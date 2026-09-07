// NVSwitch Fabric - seeded property/randomized tests with invariant checking.
// Emits seed + operation index on failure (RNG stream is reproducible from seed).
#include "test_common.h"
#include "nvswitch_fabric/fabric_registry.h"
#include "nvswitch_fabric/backends/synthetic_backend.h"
#include <random>
#include <map>
#include <sstream>

using namespace nvswitch_fabric;

namespace {
struct InvariantState {
    std::map<SwitchId, uint64_t> maxSwitchGen;
    std::map<PortId, uint64_t> maxPortGen;
    std::map<LinkId, uint64_t> maxLinkGen;
    std::map<PartitionId, uint64_t> maxPartGen;
};

void checkInvariants(FabricRegistry& reg, InvariantState& st, uint64_t seed, uint64_t idx) {
    const auto snap = reg.snapshot();
    auto rep = snap->checkIntegrity();
    if (!rep.ok()) { std::ostringstream os; os << "integrity seed=" << seed << " idx=" << idx << " : " << rep.violations.front(); throw TestFailure{ os.str() }; }
    for (auto& [id, rec] : snap->switches()) { auto it = st.maxSwitchGen.find(id); if (it == st.maxSwitchGen.end()) st.maxSwitchGen[id] = rec.gen.get(); else if (rec.gen.get() < it->second) { std::ostringstream os; os << "switch gen decreased seed=" << seed << " idx=" << idx; throw TestFailure{ os.str() }; } else st.maxSwitchGen[id] = rec.gen.get(); }
    for (auto& [id, rec] : snap->ports()) { auto it = st.maxPortGen.find(id); if (it == st.maxPortGen.end()) st.maxPortGen[id] = rec.gen.get(); else if (rec.gen.get() < it->second) { std::ostringstream os; os << "port gen decreased seed=" << seed << " idx=" << idx; throw TestFailure{ os.str() }; } else st.maxPortGen[id] = rec.gen.get(); }
    for (auto& [id, rec] : snap->links()) { auto it = st.maxLinkGen.find(id); if (it == st.maxLinkGen.end()) st.maxLinkGen[id] = rec.gen.get(); else if (rec.gen.get() < it->second) { std::ostringstream os; os << "link gen decreased seed=" << seed << " idx=" << idx; throw TestFailure{ os.str() }; } else st.maxLinkGen[id] = rec.gen.get(); }
    for (auto& [id, rec] : snap->partitions()) { auto it = st.maxPartGen.find(id); if (it == st.maxPartGen.end()) st.maxPartGen[id] = rec.gen.get(); else if (rec.gen.get() < it->second) { std::ostringstream os; os << "partition gen decreased seed=" << seed << " idx=" << idx; throw TestFailure{ os.str() }; } else st.maxPartGen[id] = rec.gen.get(); }
    for (auto& [id, d] : snap->devices()) if (d.provenance == Provenance::REAL) { std::ostringstream os; os << "synthetic became REAL seed=" << seed << " idx=" << idx; throw TestFailure{ os.str() }; }
}
} // namespace

TEST_CASE(property_randomized_ops) {
    constexpr uint64_t seed = 0x5EED0001ULL;
    constexpr int kOps = 400;
    std::mt19937_64 rng(seed);
    FabricRegistry reg;
    applyScenarioToRegistry(reg, ScenarioType::SINGLE_SWITCH_TWO_GPU);
    InvariantState st;
    for (int i = 0; i < kOps; ++i) {
        int op = (int)(rng() % 10);
        switch (op) {
            case 0: reg.addDevice(makeSyntheticDevice(DeviceId{(uint64_t)(2 + rng()%8)}, "Synth", EntityState::UP)); break;
            case 1: break; // reserved
            case 2: reg.setSwitchState(SwitchId{10}, (EntityState)(rng()%3==0? EntityState::DOWN : EntityState::UP), "p"); break;
            case 3: reg.setPortState(PortId{SwitchId{10}, (uint32_t)(rng()%3)}, (EntityState)(rng()%3==0? EntityState::DEGRADED : EntityState::UP), "p"); break;
            case 4: reg.publishMeasurement([&]{ Measurement m; m.source=DeviceId{1}; m.destination=DeviceId{2}; m.sourceKnown=m.destinationKnown=true; m.payloadBytes=1024*(1+rng()%4); m.iterations=3; m.elapsedNanos=1000*(1+rng()%4); m.kind=MeasurementKind::SYNTHETIC_FIXTURE; m.provenance=Provenance::SYNTHETIC; m.integrityVerified=true; return m; }()); break;
            case 5: { FabricPartition p = makeSyntheticPartition(PartitionId{"px"}, {DeviceId{1}, DeviceId{2}}, {SwitchId{10}}, PartitionState::ACTIVE); p.gen = PartitionGeneration{ (uint64_t)(1 + rng()%4) }; reg.publishPartition(p); break; }
            case 6: reg.expireDynamicEvidence(); break;
            case 7: { RoutePolicy pol; auto d = reg.route(DeviceId{1}, DeviceId{2}, pol); CHECK(d.ok()); auto e = reg.route(DeviceId{1}, DeviceId{2}, pol); std::string k1 = d.value().selected?d.value().selected->canonicalKey():""; std::string k2 = e.value().selected?e.value().selected->canonicalKey():""; if (k1 != k2) { std::ostringstream os; os << "nondeterministic route seed=" << seed << " idx=" << i; throw TestFailure{ os.str() }; } break; }
            case 8: reg.setSwitchState(SwitchId{11}, EntityState::UP, "p"); break;
            case 9: { auto snap = reg.snapshot(); if (snap->deviceCount() >= 8) reg.removeDevice(DeviceId{(uint64_t)(2 + rng()%8)}); break; }
        }
        checkInvariants(reg, st, seed, (uint64_t)i);
    }
}

TEST_CASE(property_dense_topology_bounded) {
    FabricRegistry reg;
    applyScenarioToRegistry(reg, ScenarioType::DENSE_MULTI_SWITCH);
    TraversalConfig cfg; cfg.maxPaths = 512; cfg.maxDepth = 8;
    auto paths = reg.paths(DeviceId{1}, DeviceId{4}, cfg);
    CHECK(paths.size() <= 512);
    const auto snap = reg.snapshot();
    CHECK(snap->enumeratePaths(DeviceId{1}, DeviceId{4}, cfg).size() <= 512);
}

NVF_MAIN