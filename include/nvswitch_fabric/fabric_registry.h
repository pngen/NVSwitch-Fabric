// NVSwitch Fabric - FabricRegistry (transactional, copy-on-write state).
#pragma once
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <deque>
#include <optional>
#include "nvswitch_fabric/topology.h"
#include "nvswitch_fabric/route.h"
#include "nvswitch_fabric/evidence.h"
#include "nvswitch_fabric/backend.h"
#include "nvswitch_fabric/error.h"

namespace nvswitch_fabric {

// Coarse lock-reentrancy rule: mutation methods take the exclusive lock only
// briefly to copy + swap the immutable snapshot. Route/path/health computation
// always runs on a snapshot copy with NO lock held (immutable read), so a
// route query can never be re-entered from a mutation and vice versa.
class FabricRegistry {
public:
    using Snapshot = std::shared_ptr<const FabricTopology>;

    FabricRegistry();
    FabricRegistry(const FabricRegistry&) = delete;
    FabricRegistry& operator=(const FabricRegistry&) = delete;

    // --- snapshot / authority ---
    Snapshot snapshot() const;
    CoordinatorEpoch epoch() const noexcept { return epoch_; }
    void setEpoch(CoordinatorEpoch e) noexcept { epoch_ = e; }
    uint64_t nextSnapshotId();
    uint64_t nextDecisionId();
    uint64_t nextObservationId();
    uint64_t nextMeasurementId();
    int64_t snapshotVersion() const noexcept { return version_; }
    uint64_t lastSnapshotDerivedNanos() const noexcept { return lastSnapshotNanos_; }

    // --- transactional mutation (each returns the new snapshot) ---
    Result<Snapshot> addDevice(const DeviceRecord& rec);
    Result<Snapshot> removeDevice(DeviceId id);
    Result<Snapshot> addSwitch(const SwitchRecord& rec);
    Result<Snapshot> removeSwitch(SwitchId id);
    Result<Snapshot> setSwitchState(SwitchId id, EntityState state, std::string reason);
    Result<Snapshot> restartSwitch(SwitchId id, SwitchGeneration newGen); // advance gen; stale dynamic
    Result<Snapshot> addPort(const SwitchPortRecord& rec);
    Result<Snapshot> removePort(PortId id);
    Result<Snapshot> setPortState(PortId id, EntityState state, std::string reason);
    Result<Snapshot> addLink(const LinkRecord& rec);
    Result<Snapshot> removeLink(LinkId id);
    Result<Snapshot> setLinkState(LinkId id, EntityState state, bool degraded, std::string reason);
    Result<Snapshot> publishPartition(const FabricPartition& part); // upsert, generation-bound
    Result<Snapshot> removePartition(PartitionId id);
    Result<Snapshot> markPartitionStale(PartitionId id);

    // --- dynamic evidence (measurement/congestion) ---
    Result<Snapshot> publishMeasurement(const Measurement& m);
    Result<Snapshot> publishCongestion(const CongestionObservation& c);
    Result<void> recordDecision(const RouteDecision& d);

    // --- authority / fencing ---
    Result<Snapshot> invalidateForWorker(WorkerId worker, WorkerBootId boot);
    Result<Snapshot> expireDynamicEvidence();
    Result<Snapshot> revalidateAll();

    // --- bulk apply (transactional) ---
    struct BulkEdit {
        std::vector<DeviceRecord> devices;
        std::vector<SwitchRecord> switches;
        std::vector<SwitchPortRecord> ports;
        std::vector<LinkRecord> links;
        std::vector<FabricPartition> partitions;
    };
    Result<Snapshot> applyBulk(const BulkEdit& e);
    Result<Snapshot> applyBackendScan(const Backend::Scan& scan);

    // --- reads (delegate to snapshot, lock-free after snapshot copy) ---
    Reachability reachability(DeviceId src, DeviceId dst) const;
    FabricHealth health() const;
    std::vector<FabricPath> paths(DeviceId src, DeviceId dst, const TraversalConfig& cfg = {}) const;

    // --- route ---
    // Computes a fresh route on the current snapshot; logs the decision.
    Result<RouteDecision> route(DeviceId src, DeviceId dst, const RoutePolicy& policy);
    // Re-check an existing decision against the current snapshot.
    bool decisionStillAuthoritative(const RouteDecision& d) const;
    // Bounded history of recent decisions, each annotated with current authority.
    std::vector<RouteDecision> recentDecisions() const;
    std::vector<Measurement> recentMeasurements() const;

    // --- backend ---
    Result<void> bindBackend(std::shared_ptr<Backend> b);
    Result<BackendCapabilities> backendCapabilities() const;
    Result<void> scanBackend();

    // --- persistence ---
    Result<void> persistTo(const std::string& path) const;
    static Result<std::shared_ptr<FabricRegistry>> restoreFrom(const std::string& path);

    const std::deque<Measurement>& measurementHistory() const noexcept { return measurements_; }

private:
    friend class Persistence;

    // The lock serializes only the swap of the immutable snapshot; readers
    // copy the shared_ptr under the shared lock then release it.
    mutable std::shared_mutex mutex_;
    Snapshot current_;
    std::deque<Measurement> measurements_;
    std::deque<RouteDecision> decisionLog_;
    std::vector<MeasurementId> measurementIds_;

    uint64_t nextSnapshotId_{1};
    uint64_t nextDecisionId_{1};
    uint64_t nextObservationId_{1};
    uint64_t nextMeasurementId_{1};
    CoordinatorEpoch epoch_{0};
    int64_t version_{0};
    uint64_t lastSnapshotNanos_{0};

    std::shared_ptr<Backend> backend_;

    // validate a candidate snapshot before committing.
    bool validateCandidate(FabricTopology& cand, std::string& why) const;
    static void finalizeProvenance(FabricTopology& cand);
};

} // namespace nvswitch_fabric
