// NVSwitch Fabric - multiprocess runtime tests: real worker death, coordinator restart,
// stale boot/epoch rejection. Uses real OS process spawn + terminate (Windows).
#include "test_common.h"
#include "nvswitch_fabric/fabric_registry.h"
#include "nvswitch_fabric/backends/synthetic_backend.h"
#include "nvswitch_fabric/runtime/coordinator.h"
#include "nvswitch_fabric/runtime/worker.h"
#include "nvswitch_fabric/runtime/transport.h"
#include <chrono>
#include <thread>
#include <fstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace nvswitch_fabric;
using namespace nvswitch_fabric::rt;

namespace {
void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

#ifdef _WIN32
struct Proc { HANDLE h = INVALID_HANDLE_VALUE; };
bool spawnProc(const std::string& exe, const std::string& args, Proc& out) {
    std::string cmdline = "\"" + exe + "\" " + args;
    STARTUPINFOA si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> buf(cmdline.begin(), cmdline.end()); buf.push_back(0);
    if (!CreateProcessA(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    out.h = pi.hProcess; CloseHandle(pi.hThread);
    return true;
}
void killProc(Proc& p) { if (p.h != INVALID_HANDLE_VALUE) { TerminateProcess(p.h, 0); WaitForSingleObject(p.h, 5000); CloseHandle(p.h); p.h = INVALID_HANDLE_VALUE; } }
#else
struct Proc { int pid = -1; };
bool spawnProc(const std::string&, const std::string&, Proc&) { return false; }
void killProc(Proc&) {}
#endif

uint16_t findFreePort() {
    TcpSocket s; if (!s.listenOn(0).ok()) return 0;
    uint16_t port = s.localPort(); s.close();
    return port;
}
bool fileExists(const std::string& p) { std::ifstream f(p); return f.good(); }

bool waitSwitchCount(FabricRegistry& reg, size_t n, int maxMs) {
    for (int t = 0; t < maxMs; t += 20) { if (reg.snapshot()->switchCount() >= n) return true; sleepMs(20); }
    return reg.snapshot()->switchCount() >= n;
}
} // namespace

const char* kMaybeWorker = NVF_WORKER_BIN;
const char* kMaybeCoord = NVF_COORDINATOR_BIN;

TEST_CASE(runtime_transport_socket_roundtrip) {
    TcpSocket server; CHECK(server.listenOn(0).ok());
    uint16_t port = server.localPort();
    TcpSocket client; CHECK(client.connectTo("127.0.0.1", port).ok());
    auto srv = server.accept(); CHECK(srv.ok());
    Frame f; f.type = MsgType::PING; f.payload = {7,8,9};
    auto enc = encodeFrame(f);
    CHECK(client.sendAll(enc.data(), enc.size()).ok());
    std::vector<uint8_t> buf(64); auto rr = srv.value().recvSome(buf.data(), buf.size()); CHECK(rr.ok());
    FrameStream fs; std::vector<Frame> out; CHECK(fs.feed(buf.data(), rr.value(), out)); CHECK(out.size() == 1);
    CHECK(out[0].payload.size() == 3);
    client.close(); srv.value().close(); server.close();
}

TEST_CASE(runtime_worker_death_invalidates) {
    Coordinator coord;
    coord.setEpoch(1);
    CHECK(coord.start(0).ok());
    uint16_t port = coord.port();
    Proc wp;
    CHECK(spawnProc(kMaybeWorker, "127.0.0.1 " + std::to_string(port) + " w1 1 1 1 1", wp));
    CHECK(waitSwitchCount(coord.registry(), 1, 4000));
    RoutePolicy pol;
    auto d = coord.registry().route(DeviceId{1}, DeviceId{2}, pol);
    CHECK(d.value().outcome == RouteOutcome::ROUTE_ALLOWED || d.value().outcome == RouteOutcome::ROUTE_ALLOWED_DEGRADED);
    killProc(wp);   // real OS process death.
    bool revalidated = false;
    for (int t = 0; t < 4000 && !revalidated; t += 20) {
        auto d2 = coord.registry().route(DeviceId{1}, DeviceId{2}, pol);
        if (d2.value().outcome != RouteOutcome::ROUTE_ALLOWED && d2.value().outcome != RouteOutcome::ROUTE_ALLOWED_DEGRADED) {
            revalidated = (d2.value().outcome == RouteOutcome::REVALIDATION_REQUIRED);
        }
        sleepMs(20);
    }
    CHECK(revalidated);
    CHECK(!coord.registry().decisionStillAuthoritative(d.value()));
    coord.stop();
}

TEST_CASE(runtime_coordinator_restart_revalidation) {
    uint16_t port = findFreePort();
    std::string persist = "test_coord_state.nvf";
    Proc coord1, wp1;
    CHECK(spawnProc(kMaybeCoord, std::to_string(port) + " " + persist + " 1", coord1));
    CHECK(spawnProc(kMaybeWorker, "127.0.0.1 " + std::to_string(port) + " w1 10 1 1 1", wp1));
    for (int t = 0; t < 4000 && !fileExists(persist); t += 20) sleepMs(20);
    CHECK(fileExists(persist));
    killProc(wp1); killProc(coord1);
    // Fresh coordinator with a new epoch (2) and the persisted state.
    Proc coord2;
    CHECK(spawnProc(kMaybeCoord, std::to_string(port) + " " + persist + " 2", coord2));
    Proc wp2;
    CHECK(spawnProc(kMaybeWorker, "127.0.0.1 " + std::to_string(port) + " w1 20 2 1 1", wp2));
    // Query over the wire with a probing worker (epoch=2).
    bool allowed = false;
    for (int t = 0; t < 5000 && !allowed; t += 30) {
        Worker probe;
        if (!probe.connect("127.0.0.1", port, WorkerId{"probe"}, 1u, 2).ok()) { probe.close(); sleepMs(30); continue; }
        RoutePolicy pol;
        auto dr = probe.queryRoute(DeviceId{1}, DeviceId{2}, pol, 2);
        if (dr.ok()) {
            RouteOutcome o = dr.value().outcome;
            if (o == RouteOutcome::ROUTE_ALLOWED || o == RouteOutcome::ROUTE_ALLOWED_DEGRADED) allowed = true;
        }
        probe.close();
        if (!allowed) sleepMs(30);
    }
    CHECK(allowed);
    killProc(wp2); killProc(coord2);
}


TEST_CASE(runtime_stale_boot_rejected) {
    Coordinator coord;
    coord.setEpoch(1);
    CHECK(coord.start(0).ok());
    uint16_t port = coord.port();
    // Worker A (boot=100) publishes.
    Worker a;
    CHECK(a.connect("127.0.0.1", port, WorkerId{"wstale"}, 100u, 1).ok());
    FabricRegistry reg; applyScenarioToRegistry(reg, ScenarioType::SINGLE_SWITCH_TWO_GPU);
    CHECK(a.publishTopology(*reg.snapshot(), 1).ok());
    // Wait for the topology to land.
    bool landed = false;
    for (int t = 0; t < 3000 && !landed; t += 20) { auto sw = coord.registry().snapshot()->lookupSwitch(SwitchId{10}); if (sw && sw->workerBoot == 100) landed = true; sleepMs(20); }
    CHECK(landed);
    // Stale replay: worker A, same workerId, LOWER boot (50). The coordinator must
    // reject it so the registry is not overridden.
    Worker stale;
    CHECK(stale.connect("127.0.0.1", port, WorkerId{"wstale"}, 50u, 1).ok());
    (void)stale.publishTopology(*reg.snapshot(), 1);
    sleepMs(200);
    auto sw2 = coord.registry().snapshot()->lookupSwitch(SwitchId{10});
    CHECK(sw2.has_value());
    CHECK(sw2->workerBoot == 100);   // stale boot did NOT override.
    a.close(); stale.close();
    coord.stop();
}

NVF_MAIN