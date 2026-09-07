// NVSwitch Fabric - coordinator (fabric authority + worker fencing).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <map>
#include "nvswitch_fabric/fabric_registry.h"
#include "nvswitch_fabric/runtime/transport.h"

namespace nvswitch_fabric {
namespace rt {

// The coordinator owns the authoritative FabricRegistry, accepts worker
// publications over a bounded framed TCP transport, and fences worker-owned
// evidence when a worker's connection closes (real OS-process death).
class Coordinator {
public:
    Coordinator();
    ~Coordinator();
    Coordinator(const Coordinator&) = delete;
    Coordinator& operator=(const Coordinator&) = delete;

    Result<void> start(uint16_t port);
    void stop();

    FabricRegistry& registry() noexcept { return registry_; }
    const FabricRegistry& registry() const noexcept { return registry_; }

    CoordinatorEpoch epoch() const noexcept { return registry_.epoch(); }
    void setEpoch(CoordinatorEpoch e) noexcept { registry_.setEpoch(e); }
    void setPersistPath(std::string p) { persistPath_ = std::move(p); }

    uint16_t port() const noexcept { return port_; }
    bool running() const noexcept { return running_.load(); }

    Result<void> persistTo(const std::string& path) const;
    // Restore durable state into this coordinator, advance the epoch, and force
    // dynamic worker-owned evidence into revalidation.
    Result<void> restoreFromAndRevalidate(const std::string& path);

    size_t liveConnections() const;

private:
    void acceptLoop();
    void handleConnection(std::shared_ptr<TcpSocket> sock, uint64_t connId);
    void applyWorkerTopology(const FabricTopology& topo, const WorkerId& worker, WorkerBootId boot);

    FabricRegistry registry_;
    TcpSocket listen_;
    uint16_t port_{0};
    std::string persistPath_;
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
    mutable std::mutex threadsMutex_;
    std::vector<std::thread> threads_;
    std::atomic<uint64_t> connSeq_{0};
    mutable std::mutex liveMutex_;
    std::vector<std::shared_ptr<TcpSocket>> liveSocks_;

    // Worker boot tracking: the highest boot seen per worker, and the boot of
    // the currently active connection per worker. A stale boot (lower than the
    // max seen) is rejected so replayed worker traffic cannot mutate state.
    mutable std::mutex authMutex_;
    std::map<WorkerId, WorkerBootId> maxBoot_;
    std::map<WorkerId, WorkerBootId> activeBoot_;
};

// Process entry point for nvswitch_fabric_coordinator.
int coordinatorMain(int argc, char** argv);

} // namespace rt
} // namespace nvswitch_fabric
