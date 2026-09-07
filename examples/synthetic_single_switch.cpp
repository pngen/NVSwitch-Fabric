// NVSwitch Fabric example: synthetic single-switch topology.
#include <iostream>
#include "nvswitch_fabric/fabric_registry.h"
#include "nvswitch_fabric/backends/synthetic_backend.h"

using namespace nvswitch_fabric;

int main() {
    FabricRegistry reg;
    auto r = applyScenarioToRegistry(reg, ScenarioType::SINGLE_SWITCH_TWO_GPU);
    if (!r.ok()) { std::cout << "scenario apply failed: " << r.error().message << "\n"; return 1; }
    auto snap = reg.snapshot();
    std::cout << "topology devices=" << snap->deviceCount() << " switches=" << snap->switchCount()
              << " ports=" << snap->portCount() << " links=" << snap->linkCount()
              << " provenance=" << toString(snap->provenance()) << "\n";
    auto rch = reg.reachability(DeviceId{1}, DeviceId{2});
    std::cout << "reachability(1->2) = " << toString(rch) << "\n";
    auto paths = reg.paths(DeviceId{1}, DeviceId{2});
    std::cout << "paths=" << paths.size() << "\n";
    for (auto& p : paths) std::cout << "  " << p.canonicalKey() << " state=" << toString(p.state) << "\n";
    RoutePolicy policy;
    auto d = reg.route(DeviceId{1}, DeviceId{2}, policy);
    std::cout << "route outcome=" << toString(d.value().outcome) << " authoritative=" << d.value().authoritative << "\n";
    if (d.value().selected) std::cout << "selected=" << d.value().selected->canonicalKey() << "\n";
    return 0;
}
