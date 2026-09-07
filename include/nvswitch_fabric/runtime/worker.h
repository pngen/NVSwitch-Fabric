// NVSwitch Fabric - worker (publishes fabric evidence to a coordinator).
#pragma once
#include <cstdint>
#include <string>
#include "nvswitch_fabric/fabric_registry.h"
#include "nvswitch_fabric/runtime/transport.h"

namespace nvswitch_fabric {
namespace rt {

// A worker connects to a coordinator, registers its (WorkerId, WorkerBootId)
// and publishes fabric evidence. It stays connected so its death is observable
// by the coordinator (real OS-process termination).
class Worker {
public:
    Worker();
    ~Worker();
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    // Connects, performs HELLO/REGISTER. Returns the negotiated protocol.
    Result<uint8_t> connect(const std::string& host, uint16_t port,
                            const WorkerId& worker, WorkerBootId boot, CoordinatorEpoch epoch);
    Result<void> publishTopology(const FabricTopology& topo, CoordinatorEpoch epoch);
    Result<void> publishMeasurement(const Measurement& m, CoordinatorEpoch epoch);
    Result<RouteDecision> queryRoute(DeviceId src, DeviceId dst, const RoutePolicy& policy, CoordinatorEpoch epoch);
    void close();
    bool connected() const noexcept { return sock_.valid(); }

private:
    Result<void> sendFrame(MsgType type, const std::vector<uint8_t>& payload);
    Result<Frame> recvFrame();
    TcpSocket sock_;
    FrameStream stream_;
    std::string host_;
    uint16_t port_{0};
    WorkerId worker_;
    WorkerBootId boot_{0};
};

// Process entry point for nvswitch_fabric_worker.
int workerMain(int argc, char** argv);

} // namespace rt
} // namespace nvswitch_fabric
