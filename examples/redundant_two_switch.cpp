// NVSwitch Fabric example: redundant two-switch topology + deterministic alternate route.
#include <iostream>
#include "nvswitch_fabric/fabric_registry.h"
#include "nvswitch_fabric/backends/synthetic_backend.h"

using namespace nvswitch_fabric;

int main() {
    FabricRegistry reg;
    auto r = applyScenarioToRegistry(reg, ScenarioType::TWO_SWITCH_REDUNDANT);
    if (!r.ok()) { std::cout << "scenario apply failed\n"; return 1; }
    auto paths = reg.paths(DeviceId{1}, DeviceId{2});
    std::cout << "candidate paths (deterministic order):\n";
    for (auto& p : paths) std::cout << "  " << p.canonicalKey() << "\n";
    RoutePolicy policy;
    auto d = reg.route(DeviceId{1}, DeviceId{2}, policy);
    std::cout << "route outcome=" << toString(d.value().outcome) << "\n";
    if (d.value().selected) std::cout << "selected=" << d.value().selected->canonicalKey() << "\n";
    // Determinism proof: run twice, compare selected key.
    auto d2 = reg.route(DeviceId{1}, DeviceId{2}, policy);
    std::string k1 = d.value().selected ? d.value().selected->canonicalKey() : "";
    std::string k2 = d2.value().selected ? d2.value().selected->canonicalKey() : "";
    std::cout << "deterministic=" << (k1 == k2 ? "yes" : "no") << "\n";
    return 0;
}
