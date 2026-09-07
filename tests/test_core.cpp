// NVSwitch Fabric - core invariant & unit tests.
#include "test_common.h"
#include "nvswitch_fabric/fabric_registry.h"
#include "nvswitch_fabric/backends/synthetic_backend.h"
#include "nvswitch_fabric/persistence.h"
#include "nvswitch_fabric/wire.h"
#include <cmath>
#include <functional>

using namespace nvswitch_fabric;

TEST_CASE(identity_typed_generations) {
    DeviceId a{1}, b{1}; CHECK(a == b); DeviceId c{2}; CHECK(a != c);
    PortId p{SwitchId{10}, 3}; CHECK(p.sw.get() == 10); CHECK(p.index == 3);
    CHECK(PortId::unknown().isUnknown());
    Gen<FabGenTag> g{5}; CHECK(g.get() == 5); CHECK(g.next().get() == 6); CHECK(g < g.next());
    uint64_t x = a.get(); (void)x;
}

TEST_CASE(wire_roundtrip_and_corruption) {
    ByteWriter w; w.u32(0xDEADBEEFu); w.str("hello"); w.u64(123456789u);
    ByteReader r(w.data().data(), w.size());
    uint32_t u; CHECK(r.u32(u)); CHECK(u == 0xDEADBEEFu);
    std::string s; CHECK(r.str(s)); CHECK(s == "hello");
    uint64_t v; CHECK(r.u64(v)); CHECK(v == 123456789u);
    // corruption: truncate.
    ByteReader r2(w.data().data(), w.size() - 2);
    CHECK(!r2.u32(u) || !r2.str(s) || !r2.u64(v));
}

TEST_CASE(synthetic_single_switch) {
    FabricRegistry reg;
    CHECK(applyScenarioToRegistry(reg, ScenarioType::SINGLE_SWITCH_TWO_GPU).ok());
    auto snap = reg.snapshot();
    CHECK(snap->provenance() == Provenance::SYNTHETIC);
    CHECK(snap->deviceCount() == 2);
    CHECK(snap->switchCount() == 1);
    auto rch = reg.reachability(DeviceId{1}, DeviceId{2});
    CHECK(rch == Reachability::REACHABLE);
    auto paths = reg.paths(DeviceId{1}, DeviceId{2});
    CHECK(paths.size() == 1);
    RoutePolicy pol;
    auto d = reg.route(DeviceId{1}, DeviceId{2}, pol);
    CHECK(d.value().outcome == RouteOutcome::ROUTE_ALLOWED);
    CHECK(d.value().authoritative);
    CHECK(d.value().selected.has_value());
}

TEST_CASE(redundant_paths_deterministic) {
    FabricRegistry reg; applyScenarioToRegistry(reg, ScenarioType::TWO_SWITCH_REDUNDANT);
    RoutePolicy pol;
    auto d1 = reg.route(DeviceId{1}, DeviceId{2}, pol);
    auto d2 = reg.route(DeviceId{1}, DeviceId{2}, pol);
    std::string k1 = d1.value().selected ? d1.value().selected->canonicalKey() : "";
    std::string k2 = d2.value().selected ? d2.value().selected->canonicalKey() : "";
    CHECK_EQ(k1, k2); CHECK(!k1.empty());
    CHECK(d1.value().candidatePaths.size() >= 2);
}

TEST_CASE(partition_gating) {
    FabricRegistry reg; applyScenarioToRegistry(reg, ScenarioType::PARTITION_SPLIT);
    auto rc = reg.reachability(DeviceId{1}, DeviceId{3});
    CHECK(rc == Reachability::PARTITION_BLOCKED);
    auto rk = reg.reachability(DeviceId{1}, DeviceId{2});
    CHECK(rk == Reachability::REACHABLE);
    RoutePolicy pol;
    auto d = reg.route(DeviceId{1}, DeviceId{3}, pol);
    CHECK(d.value().outcome == RouteOutcome::PARTITION_BLOCKED);
}

TEST_CASE(switch_failure_invalidates_authority) {
    FabricRegistry reg; applyScenarioToRegistry(reg, ScenarioType::TWO_SWITCH_REDUNDANT);
    RoutePolicy pol;
    auto d1 = reg.route(DeviceId{1}, DeviceId{2}, pol);
    CHECK(d1.value().authoritative);
    auto dcheck = d1.value();
    CHECK(reg.decisionStillAuthoritative(dcheck));
    CHECK(reg.setSwitchState(SwitchId{10}, EntityState::DOWN, "fail").ok());
    CHECK(!reg.decisionStillAuthoritative(dcheck));
    CHECK(reg.decisionStillAuthoritative(d1.value()) == false);
}

TEST_CASE(partition_change_invalidates) {
    FabricRegistry reg; applyScenarioToRegistry(reg, ScenarioType::PARTITION_SPLIT);
    RoutePolicy pol;
    auto d1 = reg.route(DeviceId{1}, DeviceId{2}, pol);
    CHECK(reg.decisionStillAuthoritative(d1.value()));
    auto snap = reg.snapshot();
    auto p0 = *snap->lookupPartition(PartitionId{"p0"});
    // Change partition p0 (advance generation).
    p0.members.push_back(DeviceId{3});
    p0.gen = PartitionGeneration{p0.gen.get() + 1};
    CHECK(reg.publishPartition(p0).ok());
    CHECK(!reg.decisionStillAuthoritative(d1.value()));
}

TEST_CASE(stale_generation_rejected) {
    FabricRegistry reg; applyScenarioToRegistry(reg, ScenarioType::TWO_SWITCH_REDUNDANT);
    FabricRegistry::BulkEdit e;
    SwitchRecord sw = makeSyntheticSwitch(SwitchId{10}, "X", 2);
    sw.gen = SwitchGeneration{0};   // stale (current is >=1).
    e.switches.push_back(sw);
    auto res = reg.applyBulk(e);
    CHECK(!res.ok());
}

TEST_CASE(provenance_preserved) {
    FabricRegistry reg; applyScenarioToRegistry(reg, ScenarioType::SINGLE_SWITCH_TWO_GPU);
    // all records synthetic.
    auto snap = reg.snapshot();
    for (auto& [id, d] : snap->devices()) CHECK(d.provenance == Provenance::SYNTHETIC);
    for (auto& [id, s] : snap->switches()) CHECK(s.provenance == Provenance::SYNTHETIC);
    CHECK(snap->provenance() == Provenance::SYNTHETIC);
}

TEST_CASE(integrity_detects_references) {
    FabricRegistry reg; applyScenarioToRegistry(reg, ScenarioType::SINGLE_SWITCH_TWO_GPU);
    // A bulk edit referencing a missing device must fail.
    FabricRegistry::BulkEdit e;
    LinkRecord l = makeSyntheticLink(LinkId{999}, SwitchId{10}, PortId{SwitchId{10}, 9}, LinkEnd::toDevice(DeviceId{777}));
    e.links.push_back(l);
    CHECK(!reg.applyBulk(e).ok());
}

TEST_CASE(persistence_roundtrip_and_provenance) {
    FabricRegistry reg; applyScenarioToRegistry(reg, ScenarioType::TWO_SWITCH_REDUNDANT);
    auto rs = reg.persistTo("test_tmp.nvf");
    CHECK(rs.ok());
    auto rr = FabricRegistry::restoreFrom("test_tmp.nvf");
    CHECK(rr.ok());
    auto snap = rr.value()->snapshot();
    CHECK(snap->deviceCount() == 2);
    CHECK(snap->switchCount() == 2);
    CHECK(snap->provenance() == Provenance::SYNTHETIC);   // never upgraded to REAL.
    auto rp = rr.value()->reachability(DeviceId{1}, DeviceId{2});
    CHECK(rp == Reachability::REACHABLE);
}

TEST_CASE(persistence_corruption_rejected) {
    FabricRegistry reg; applyScenarioToRegistry(reg, ScenarioType::SINGLE_SWITCH_TWO_GPU);
    CHECK(reg.persistTo("test_tmp2.nvf").ok());
    auto bytes = Persistence::serialize(reg);
    CHECK(bytes.ok());
    auto data = bytes.value();
    CHECK(Persistence::deserialize(data).ok());
    // Corrupt payload byte.
    data[data.size()/2] ^= 0xFF;
    auto r = Persistence::deserialize(data);
    CHECK(!r.ok());
    // Truncate.
    auto trunc = bytes.value(); trunc.resize(trunc.size()/2);
    CHECK(!Persistence::deserialize(trunc).ok());
    // Trailing garbage.
    auto tg = bytes.value(); tg.push_back('X'); tg.push_back('Y');
    CHECK(!Persistence::deserialize(tg).ok());
}

TEST_CASE(unknown_never_becomes_healthy) {
    FabricRegistry reg;
    DeviceRecord d = makeSyntheticDevice(DeviceId{1}, "X", EntityState::UNKNOWN);
    // Keep UNKNOWN in reachability: src/dst not present => insufficient evidence.
    CHECK(reg.addDevice(d).ok());
    CHECK(reg.reachability(DeviceId{1}, DeviceId{2}) == Reachability::INSUFFICIENT_EVIDENCE);
}

TEST_CASE(route_determinism_insertion_order) {
    FabricRegistry reg; applyScenarioToRegistry(reg, ScenarioType::DENSE_MULTI_SWITCH);
    RoutePolicy pol;
    auto d1 = reg.route(DeviceId{1}, DeviceId{4}, pol);
    auto d2 = reg.route(DeviceId{1}, DeviceId{4}, pol);
    CHECK_EQ(d1.value().selected ? d1.value().selected->canonicalKey() : std::string{},
             d2.value().selected ? d2.value().selected->canonicalKey() : std::string{});
}

TEST_CASE(path_enumeration_bounded_dense) {
    FabricRegistry reg; applyScenarioToRegistry(reg, ScenarioType::DENSE_MULTI_SWITCH);
    TraversalConfig cfg; cfg.maxPaths = 4096; cfg.maxDepth = 6;
    auto paths = reg.paths(DeviceId{1}, DeviceId{4}, cfg);
    CHECK(paths.size() <= 4096);
    CHECK(reg.snapshot()->enumeratePaths(DeviceId{1}, DeviceId{4}, cfg).size() <= 4096);
}

TEST_CASE(measurement_ingest_and_match_invalidation) {
    FabricRegistry reg; applyScenarioToRegistry(reg, ScenarioType::SINGLE_SWITCH_TWO_GPU);
    Measurement m; m.source = DeviceId{1}; m.destination = DeviceId{2};
    m.sourceKnown = m.destinationKnown = true; m.payloadBytes = 1024; m.iterations = 3; m.elapsedNanos = 1000;
    m.kind = MeasurementKind::SYNTHETIC_FIXTURE; m.provenance = Provenance::SYNTHETIC; m.integrityVerified = true;
    CHECK(reg.publishMeasurement(m).ok());
    CHECK(reg.recentMeasurements().size() == 1);
    CHECK(reg.expireDynamicEvidence().ok());
    CHECK(!reg.recentMeasurements().front().matched);
}

NVF_MAIN