// NVSwitch Fabric - the fabric topology snapshot graph.
// This is an immutable, copy-on-write snapshot. The registry publishes new
// snapshots atomically; readers hold a shared_ptr<const FabricTopology> and
// read lock-free. All containers are ordered (std::map) so traversal and
// ranking are deterministic.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include "nvswitch_fabric/identity.h"
#include "nvswitch_fabric/generation.h"
#include "nvswitch_fabric/provenance.h"
#include "nvswitch_fabric/state.h"
#include "nvswitch_fabric/records.h"
#include "nvswitch_fabric/limits.h"
#include "nvswitch_fabric/evidence.h"

namespace nvswitch_fabric {

// Forward decls.
struct FabricPath;
struct CongestionObservation;
class Backend;

// Controls path search and ranking bounds.
struct TraversalConfig {
    uint32_t maxPaths{limits::kMaxCandidatePaths};
    uint32_t maxDepth{limits::kMaxPathDepth};
    bool allowSwitchSwitching{true};   // allow switch-to-switch hops.
    double requiredMinRelativeBandwidth{0.0}; // 0 => no requirement.
};

class FabricTopology {
public:
    // --- snapshot identity ---
    uint64_t idValue() const noexcept { return id_; }
    TopologyGeneration topologyGen() const noexcept { return topologyGen_; }
    FabricGeneration fabricGen() const noexcept { return fabricGen_; }
    CoordinatorEpoch epoch() const noexcept { return epoch_; }
    Provenance provenance() const noexcept { return provenance_; }
    const std::string& label() const noexcept { return label_; }
    bool persistedKnown() const noexcept { return persisted_; }

    // --- accessors (ordered, deterministic) ---
    const std::map<DeviceId, DeviceRecord>& devices() const noexcept { return devices_; }
    const std::map<SwitchId, SwitchRecord>& switches() const noexcept { return switches_; }
    const std::map<PortId, SwitchPortRecord>& ports() const noexcept { return ports_; }
    const std::map<LinkId, LinkRecord>& links() const noexcept { return links_; }
    const std::map<PartitionId, FabricPartition>& partitions() const noexcept { return partitions_; }

    std::optional<DeviceRecord> lookupDevice(DeviceId) const;
    std::optional<SwitchRecord> lookupSwitch(SwitchId) const;
    std::optional<SwitchPortRecord> lookupPort(PortId) const;
    std::optional<LinkRecord> lookupLink(LinkId) const;
    std::optional<FabricPartition> lookupPartition(PartitionId) const;

    // --- reachability ---
    Reachability deviceReachability(DeviceId src, DeviceId dst) const;

    // --- path enumeration (deterministic, bounded) ---
    // Returns up to config.maxPaths distinct switch-mediated paths from
    // source to destination, in deterministic order.
    std::vector<FabricPath> enumeratePaths(DeviceId src, DeviceId dst,
                                           const TraversalConfig& cfg = {}) const;

    // --- fabric health ---
    FabricHealth fabricHealth() const;
    std::vector<SwitchHealthObservation> healthObservations() const;

    // --- invariant checks used by the registry and tests ---
    struct IntegrityReport {
        std::vector<std::string> violations;
        bool ok() const noexcept { return violations.empty(); }
    };
    IntegrityReport checkIntegrity() const;

    // Counts.
    size_t deviceCount() const noexcept { return devices_.size(); }
    size_t switchCount() const noexcept { return switches_.size(); }
    size_t portCount() const noexcept { return ports_.size(); }
    size_t linkCount() const noexcept { return links_.size(); }
    size_t partitionCount() const noexcept { return partitions_.size(); }

private:
    friend class FabricRegistry;
    friend class Persistence;

    uint64_t id_{0};
    TopologyGeneration topologyGen_;
    FabricGeneration fabricGen_;
    CoordinatorEpoch epoch_{0};
    Provenance provenance_{Provenance::UNSUPPORTED};
    std::string label_;
    bool persisted_{false};

    std::map<DeviceId, DeviceRecord> devices_;
    std::map<SwitchId, SwitchRecord> switches_;
    std::map<PortId, SwitchPortRecord> ports_;
    std::map<LinkId, LinkRecord> links_;
    std::map<PartitionId, FabricPartition> partitions_;
};

} // namespace nvswitch_fabric
