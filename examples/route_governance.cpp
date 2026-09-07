// NVSwitch Fabric example: route authority under switch failure, partition change, stale generation.
#include <iostream>
#include "nvswitch_fabric/fabric_registry.h"
#include "nvswitch_fabric/backends/synthetic_backend.h"

using namespace nvswitch_fabric;

int main() {
    FabricRegistry reg;
    auto r = applyScenarioToRegistry(reg, ScenarioType::TWO_SWITCH_REDUNDANT);
    if (!r.ok()) return 1;
    RoutePolicy pol;
    auto d = reg.route(DeviceId{1}, DeviceId{2}, pol);
    bool allowed1 = (d.value().outcome == RouteOutcome::ROUTE_ALLOWED || d.value().outcome == RouteOutcome::ROUTE_ALLOWED_DEGRADED);
    std::cout << "phase1 route allowed=" << allowed1 << " auth=" << d.value().authoritative << "\n";
    const auto* sel = d.value().selected ? &*d.value().selected : nullptr;
    if (sel) {
        auto srid = sel->switchSequence.front();
        reg.setSwitchState(srid, EntityState::DOWN, "injected failure");
        auto d2 = reg.route(DeviceId{1}, DeviceId{2}, pol);
        bool allowed2 = (d2.value().outcome == RouteOutcome::ROUTE_ALLOWED || d2.value().outcome == RouteOutcome::ROUTE_ALLOWED_DEGRADED);
        std::cout << "phase2 (switch down) route allowed=" << allowed2 << " outcome=" << toString(d2.value().outcome) << "\n";
        bool stillAuth = reg.decisionStillAuthoritative(d.value());
        std::cout << "old decision still authoritative=" << stillAuth << "\n";
    }
    // Stale generation rejection: republish switch 10 at a LOWER generation.
    FabricRegistry::BulkEdit e;
    SwitchRecord sw = makeSyntheticSwitch(SwitchId{10}, "SynthSwitch-1", 2);
    sw.gen = SwitchGeneration{0};   // stale => must be rejected (fail closed).
    e.switches.push_back(sw);
    auto res = reg.applyBulk(e);
    std::cout << "stale generation republish accepted=" << (res.ok()?1:0) << " (expected 0) reason=" << (res.ok()?"":res.error().message) << "\n";
    return 0;
}
