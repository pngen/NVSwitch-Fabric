// NVSwitch Fabric - regression tests: route authority follows the selected path's links; a
// superseded worker boot cannot publish; a restarted worker can republish after its predecessor
// died; a coordinator restart moves past the persisted epoch; a failed persist is reported.
#include "test_common.h"
#include "nvswitch_fabric/fabric_registry.h"
#include "nvswitch_fabric/backends/synthetic_backend.h"
#include "nvswitch_fabric/runtime/coordinator.h"
#include "nvswitch_fabric/runtime/worker.h"
#include "nvswitch_fabric/runtime/transport.h"
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

using namespace nvswitch_fabric;
using namespace nvswitch_fabric::rt;

TEST_CASE(regression_route_authority_follows_path_links) {
    RoutePolicy pol;
    FabricRegistry reg;
    CHECK(applyScenarioToRegistry(reg, ScenarioType::SINGLE_SWITCH_TWO_GPU).ok());
    auto d = reg.route(DeviceId{1}, DeviceId{2}, pol);
    CHECK(d.ok() && d.value().outcome == RouteOutcome::ROUTE_ALLOWED);
    CHECK(reg.decisionStillAuthoritative(d.value()));
    // The link to the destination goes DOWN; its switch and ports are unchanged.
    CHECK(reg.setLinkState(LinkId{101}, EntityState::DOWN, false, "cable pulled").ok());
    CHECK(!reg.decisionStillAuthoritative(d.value()));

    // A path link that has been removed invalidates the decision too.
    FabricRegistry reg2;
    CHECK(applyScenarioToRegistry(reg2, ScenarioType::SINGLE_SWITCH_TWO_GPU).ok());
    auto d2 = reg2.route(DeviceId{1}, DeviceId{2}, pol);
    CHECK(d2.ok() && reg2.decisionStillAuthoritative(d2.value()));
    CHECK(reg2.removeLink(LinkId{101}).ok());
    CHECK(!reg2.decisionStillAuthoritative(d2.value()));
}

TEST_CASE(regression_superseded_boot_cannot_publish) {
    WsaGuard wsa;
    Coordinator coord;
    coord.setEpoch(1);
    CHECK(coord.start(0).ok());
    const uint16_t port = coord.port();
    FabricRegistry scen;
    CHECK(applyScenarioToRegistry(scen, ScenarioType::SINGLE_SWITCH_TWO_GPU).ok());
    RoutePolicy pol;
    Worker oldBoot;
    CHECK(oldBoot.connect("127.0.0.1", port, WorkerId{"wsup"}, 100u, 1).ok());
    CHECK(oldBoot.publishTopology(*scen.snapshot(), 1).ok());
    (void)oldBoot.queryRoute(DeviceId{1}, DeviceId{2}, pol, 1);  // barrier: one connection's frames are handled in order
    Worker newBoot;
    CHECK(newBoot.connect("127.0.0.1", port, WorkerId{"wsup"}, 200u, 1).ok());
    CHECK(newBoot.publishTopology(*scen.snapshot(), 1).ok());
    (void)newBoot.queryRoute(DeviceId{1}, DeviceId{2}, pol, 1);
    auto sw = coord.registry().snapshot()->lookupSwitch(SwitchId{10});
    CHECK(sw.has_value() && sw->workerBoot == 200u);
    // The superseded incarnation is still connected and publishes again.
    CHECK(oldBoot.publishTopology(*scen.snapshot(), 1).ok());
    (void)oldBoot.queryRoute(DeviceId{1}, DeviceId{2}, pol, 1);
    sw = coord.registry().snapshot()->lookupSwitch(SwitchId{10});
    CHECK(sw.has_value() && sw->workerBoot == 200u);
    oldBoot.close();
    newBoot.close();
    coord.stop();
}

TEST_CASE(regression_restarted_worker_republishes_after_death) {
    WsaGuard wsa;
    Coordinator coord;
    coord.setEpoch(1);
    CHECK(coord.start(0).ok());
    const uint16_t port = coord.port();
    FabricRegistry scen;
    CHECK(applyScenarioToRegistry(scen, ScenarioType::SINGLE_SWITCH_TWO_GPU).ok());
    RoutePolicy pol;
    auto first = std::make_unique<Worker>();
    CHECK(first->connect("127.0.0.1", port, WorkerId{"wre"}, 1u, 1).ok());
    CHECK(first->publishTopology(*scen.snapshot(), 1).ok());
    (void)first->queryRoute(DeviceId{1}, DeviceId{2}, pol, 1);  // barrier
    auto before = coord.registry().route(DeviceId{1}, DeviceId{2}, pol);
    CHECK(before.ok() && before.value().outcome == RouteOutcome::ROUTE_ALLOWED);
    // The first incarnation dies; wait until the coordinator has invalidated its evidence.
    first->close();
    while (coord.registry().snapshot()->lookupSwitch(SwitchId{10})->state != EntityState::REVALIDATION_REQUIRED)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    // A fresh incarnation republishes the same fabric.
    Worker second;
    CHECK(second.connect("127.0.0.1", port, WorkerId{"wre"}, 2u, 1).ok());
    CHECK(second.publishTopology(*scen.snapshot(), 1).ok());
    (void)second.queryRoute(DeviceId{1}, DeviceId{2}, pol, 1);  // barrier
    auto sw = coord.registry().snapshot()->lookupSwitch(SwitchId{10});
    CHECK(sw.has_value() && sw->workerBoot == 2u && sw->state == EntityState::UP);
    auto after = coord.registry().route(DeviceId{1}, DeviceId{2}, pol);
    CHECK(after.ok() && after.value().outcome == RouteOutcome::ROUTE_ALLOWED);
    // The decision made under the dead incarnation stays non-authoritative.
    CHECK(!coord.registry().decisionStillAuthoritative(before.value()));
    second.close();
    coord.stop();
}

TEST_CASE(regression_restart_moves_past_persisted_epoch) {
    const std::string path = "regression_epoch_state.nvf";
    std::remove(path.c_str());
    {
        Coordinator first;
        first.setEpoch(1);
        CHECK(applyScenarioToRegistry(first.registry(), ScenarioType::SINGLE_SWITCH_TWO_GPU).ok());
        CHECK(first.persistTo(path).ok());
    }
    Coordinator second;
    second.setEpoch(1);  // restarted without a new epoch
    CHECK(second.restoreFromAndRevalidate(path).ok());
    CHECK(second.epoch() > 1u);
    std::remove(path.c_str());
}

TEST_CASE(regression_failed_persist_is_reported) {
    FabricRegistry reg;
    CHECK(applyScenarioToRegistry(reg, ScenarioType::SINGLE_SWITCH_TWO_GPU).ok());
    // The directory does not exist: nothing can be written, and persistTo must say so.
    CHECK(!reg.persistTo("no_such_directory_for_regression/state.nvf").ok());
    const std::string path = "regression_persist_state.nvf";
    std::remove(path.c_str());
    CHECK(reg.persistTo(path).ok());
    CHECK(reg.persistTo(path).ok());  // replacing an existing state file
    CHECK(FabricRegistry::restoreFrom(path).ok());
    std::remove(path.c_str());
}

NVF_MAIN
