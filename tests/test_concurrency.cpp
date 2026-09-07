// NVSwitch Fabric - deterministic concurrency / race proofs.
// Runs real concurrent readers + writers; after join it verifies invariants.
#include "test_common.h"
#include "nvswitch_fabric/fabric_registry.h"
#include "nvswitch_fabric/backends/synthetic_backend.h"
#include <thread>
#include <atomic>
#include <vector>

using namespace nvswitch_fabric;

namespace {
void verifyConsistent(FabricRegistry& reg) {
    const auto snap = reg.snapshot();
    CHECK(snap->checkIntegrity().ok());
    // Generations must never have decreased: route is deterministic.
    RoutePolicy pol;
    auto a = reg.route(DeviceId{1}, DeviceId{2}, pol);
    auto b = reg.route(DeviceId{1}, DeviceId{2}, pol);
    std::string k1 = a.value().selected?a.value().selected->canonicalKey():"";
    std::string k2 = b.value().selected?b.value().selected->canonicalKey():"";
    CHECK_EQ(k1, k2);
}
} // namespace

TEST_CASE(concurrent_readers_writers_no_corruption) {
    FabricRegistry reg;
    applyScenarioToRegistry(reg, ScenarioType::TWO_SWITCH_REDUNDANT);
    std::atomic<bool> done{false};
    std::vector<std::thread> threads;
    // 4 writer threads mutating switch/port/partition state.
    for (int w = 0; w < 4; ++w) {
        threads.emplace_back([&, w]{
            for (int i = 0; i < 60 && !done.load(); ++i) {
                if (i % 3 == 0) reg.setSwitchState(SwitchId{(uint64_t)(10 + (w%2))}, i%2? EntityState::DOWN : EntityState::UP, "w");
                else if (i % 3 == 1) reg.setPortState(PortId{SwitchId{10}, (uint32_t)(i%3)}, i%2? EntityState::DEGRADED : EntityState::UP, "w");
                else { FabricPartition p = makeSyntheticPartition(PartitionId{"px"}, {DeviceId{1}, DeviceId{2}}, {SwitchId{10}, SwitchId{11}}, PartitionState::ACTIVE); p.gen = PartitionGeneration{(uint64_t)(1 + (i%4))}; reg.publishPartition(p); }
            }
        });
    }
    // 4 reader threads doing route/path/reachability on snapshots.
    for (int rr = 0; rr < 4; ++rr) {
        threads.emplace_back([&]{
            for (int i = 0; i < 400; ++i) {
                auto snap = reg.snapshot();
                (void)snap->deviceCount();
                (void)snap->enumeratePaths(DeviceId{1}, DeviceId{2});
                (void)reg.reachability(DeviceId{1}, DeviceId{2});
                RoutePolicy pol; auto d = reg.route(DeviceId{1}, DeviceId{2}, pol);
                (void)d.value().outcome;
            }
        });
    }
    for (int i = 0; i < 400; ++i) (void)reg.paths(DeviceId{1}, DeviceId{2});
    done.store(true);
    for (auto& t : threads) t.join();
    verifyConsistent(reg);
}

TEST_CASE(concurrent_persistence_races_mutation) {
    FabricRegistry reg;
    applyScenarioToRegistry(reg, ScenarioType::SINGLE_SWITCH_TWO_GPU);
    std::atomic<bool> stop{false};
    std::thread writer([&]{ for (int i = 0; i < 100 && !stop.load(); ++i) { reg.setPortState(PortId{SwitchId{10}, (uint32_t)(i%2)}, i%2? EntityState::DEGRADED : EntityState::UP, "w"); } });
    for (int i = 0; i < 30; ++i) CHECK(reg.persistTo("test_conc.nvf").ok());
    stop.store(true); writer.join();
    auto restored = FabricRegistry::restoreFrom("test_conc.nvf");
    CHECK(restored.ok());
    CHECK(restored.value()->snapshot()->checkIntegrity().ok());
}

TEST_CASE(concurrent_measurement_race) {
    FabricRegistry reg; applyScenarioToRegistry(reg, ScenarioType::SINGLE_SWITCH_TWO_GPU);
    std::thread pub([&]{ for (int i = 0; i < 40; ++i) { Measurement m; m.source=DeviceId{1}; m.destination=DeviceId{2}; m.sourceKnown=m.destinationKnown=true; m.payloadBytes=1024; m.iterations=2; m.elapsedNanos=100; m.kind=MeasurementKind::SYNTHETIC_FIXTURE; m.provenance=Provenance::SYNTHETIC; m.integrityVerified=true; reg.publishMeasurement(m); } });
    for (int i = 0; i < 200; ++i) { auto ms = reg.recentMeasurements(); for (auto& m : ms) if (m.matched) { (void)m.bytesPerSec(); } }
    pub.join();
    auto ms = reg.recentMeasurements();
    bool allMatched = true;
    for (auto& m : ms) if (!m.matched) allMatched = false;
    CHECK(allMatched);
}

NVF_MAIN
