// NVSwitch Fabric - benchmark (measured, completed work; no synthetic bandwidth claims).
#include <cstdio>
#include <chrono>
#include "nvswitch_fabric/fabric_registry.h"
#include "nvswitch_fabric/backends/synthetic_backend.h"
#include "nvswitch_fabric/persistence.h"

using namespace nvswitch_fabric;
using ns = std::chrono::nanoseconds;
using clk = std::chrono::steady_clock;

static double msns(ns d) { return (double)d.count() / 1e6; }

int main() {
    // Large synthetic topology: 16 devices, 4 switches, full device-switch adjacency.
    FabricRegistry reg;
    auto t0 = clk::now();
    applyScenarioToRegistry(reg, ScenarioType::DENSE_MULTI_SWITCH);
    auto t1 = clk::now();
    auto snap = reg.snapshot();
    std::printf("scenario install: devices=%zu switches=%zu ports=%zu links=%zu edges=%zu  %.3f ms\n",
        snap->deviceCount(), snap->switchCount(), snap->portCount(), snap->linkCount(),
        snap->linkCount(), msns(t1 - t0));

    // Snapshot cost.
    auto t2 = clk::now();
    for (int i = 0; i < 200; ++i) (void)reg.snapshot();
    auto t3 = clk::now();
    std::printf("topology snapshot x200: %.3f ms (%.2f us/op)\n", msns(t3 - t2), (double)(t3 - t2).count() / 200.0 / 1000.0);

    // Route query.
    RoutePolicy pol;
    auto t4 = clk::now();
    size_t cand = 0;
    for (int i = 0; i < 100; ++i) { auto d = reg.route(DeviceId{1}, DeviceId{4}, pol); cand += d.value().candidatePaths.size(); }
    auto t5 = clk::now();
    std::printf("route query x100: %.3f ms (%.2f us/op) total candidates=%zu\n", msns(t5 - t4), (double)(t5 - t4).count() / 100.0 / 1000.0, cand);

    // Alternate-path selection.
    TraversalConfig cfg; cfg.maxPaths = 4096; cfg.maxDepth = 8;
    auto t6 = clk::now();
    size_t paths = 0;
    for (int i = 0; i < 100; ++i) paths += reg.paths(DeviceId{1}, DeviceId{4}, cfg).size();
    auto t7 = clk::now();
    std::printf("alternate-path enumeration x100: %.3f ms (%.2f us/op) total paths=%zu\n", msns(t7 - t6), (double)(t7 - t6).count() / 100.0 / 1000.0, paths);

    // Reachability query.
    auto t8 = clk::now();
    for (int i = 0; i < 200; ++i) (void)reg.reachability(DeviceId{1}, DeviceId{4});
    auto t9 = clk::now();
    std::printf("reachability query x200: %.3f ms (%.2f us/op)\n", msns(t9 - t8), (double)(t9 - t8).count() / 200.0 / 1000.0);

    // Partition mutation.
    auto t10 = clk::now();
    for (int i = 0; i < 100; ++i) { FabricPartition p = makeSyntheticPartition(PartitionId{"b"}, {DeviceId{1}, DeviceId{2}}, {SwitchId{10}}, PartitionState::ACTIVE); p.gen = PartitionGeneration{(uint64_t)(i + 1)}; (void)reg.publishPartition(p); }
    auto t11 = clk::now();
    std::printf("partition mutation x100: %.3f ms (%.2f us/op)\n", msns(t11 - t10), (double)(t11 - t10).count() / 100.0 / 1000.0);

    // Serialization / recovery.
    {
    if (reg.persistTo("bench.nvf").ok()) {
        auto t12 = clk::now();
        for (int i = 0; i < 20; ++i) (void)FabricRegistry::restoreFrom("bench.nvf");
        auto t13 = clk::now();
        std::printf("persistence recovery x20: %.3f ms (%.2f us/op)\n", msns(t13 - t12), (double)(t13 - t12).count() / 20.0 / 1000.0);
    }
    }
    return 0;
}