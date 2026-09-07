// NVSwitch Fabric example: inspection/explanation.
#include <iostream>
#include "nvswitch_fabric/fabric_registry.h"
#include "nvswitch_fabric/backends/synthetic_backend.h"

using namespace nvswitch_fabric;

int main() {
    FabricRegistry reg;
    applyScenarioToRegistry(reg, ScenarioType::PARTITION_SPLIT);
    auto snap = reg.snapshot();
    std::cout << "provenance=" << toString(snap->provenance()) << " partitions=" << snap->partitionCount() << "\n";
    for (auto& [id,p] : snap->partitions())
        std::cout << "  partition " << p.id.value << " gen=" << p.gen.get() << " members=" << p.members.size() << " state=" << toString(p.state) << "\n";
    // Cross-partition reachability must be blocked.
    auto rr = reg.reachability(DeviceId{1}, DeviceId{3});
    std::cout << "reach(1->3)=" << toString(rr) << " (cross-partition)\n";
    return 0;
}
