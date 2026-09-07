// NVSwitch Fabric - typed topology records.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <map>
#include "nvswitch_fabric/identity.h"
#include "nvswitch_fabric/provenance.h"
#include "nvswitch_fabric/generation.h"
#include "nvswitch_fabric/state.h"

namespace nvswitch_fabric {

// A facility "generation/family" identifier, e.g. "Hopper" / "Blackwell".
struct DeviceGeneration {
    std::string name;  // e.g. "Blackwell", observed from CUDA/NVML.
    bool observed{false};
    std::string toString() const { return observed ? name : "UNKNOWN"; }
};

struct DeviceRecord {
    DeviceId id;
    DeviceGeneration generation;
    bool present{false};
    std::string backendId;             // e.g. NVML uuid / ordinal string.
    std::string vendor;
    std::string product;
    EntityState state{EntityState::UNKNOWN};
    PartitionId partition;              // membership (if known).
    SwitchId attachedSwitch;            // which switch this device attaches to (if known).
    bool attachedSwitchKnown{false};
    WorkerId worker;                    // dynamic source (fencing).
    bool workerKnown{false};
    WorkerBootId workerBoot{0};
    Provenance provenance{Provenance::UNSUPPORTED};
    EvidenceSource source{EvidenceSource::UNKNOWN};
    TopologyGeneration topologyGen;
};

struct SwitchRecord {
    SwitchId id;
    SwitchGeneration gen;
    std::string generationName;         // fabric architecture, e.g. "Tegra"-like or "NVSwitch 4".
    bool generationObserved{false};
    std::string vendor;
    std::string product;
    std::string family;
    uint32_t portCount{0};
    EntityState state{EntityState::UNKNOWN};
    FabricId fabric;
    bool fabricKnown{false};
    PartitionId partition;
    bool partitionKnown{false};
    WorkerId worker;                    // dynamic source (fencing). empty => static/derived.
    bool workerKnown{false};
    WorkerBootId workerBoot{0};
    Provenance provenance{Provenance::UNSUPPORTED};
    EvidenceSource source{EvidenceSource::UNKNOWN};
    TopologyGeneration topologyGen;
};

struct SwitchPortRecord {
    PortId id;                          // (switch, index)
    PortGeneration gen;
    LinkEnd attached;                   // device or switch on the far end.
    bool attachedKnown{false};
    EntityState state{EntityState::UNKNOWN};
    uint32_t activeLinkCount{0};
    bool activeLinkCountKnown{false};
    uint64_t nominalBandwidthMBps{0};   // nominal per-link capacity.
    bool nominalBandwidthKnown{false};
    WorkerId worker;
    bool workerKnown{false};
    WorkerBootId workerBoot{0};
    Provenance provenance{Provenance::UNSUPPORTED};
    EvidenceSource source{EvidenceSource::UNKNOWN};
    TopologyGeneration topologyGen;
};

struct LinkRecord {
    LinkId id;
    LinkGeneration gen;
    SwitchId rootSwitch;                // the switch this link is rooted at.
    PortId port;                        // the switch port this link occupies.
    LinkEnd otherEnd;                   // attached device or switch.
    bool otherEndKnown{false};
    EntityState state{EntityState::UNKNOWN};
    bool degraded{false};
    WorkerId worker;
    bool workerKnown{false};
    WorkerBootId workerBoot{0};
    Provenance provenance{Provenance::UNSUPPORTED};
    EvidenceSource source{EvidenceSource::UNKNOWN};
    TopologyGeneration topologyGen;
};

struct FabricPartition {
    PartitionId id;
    PartitionGeneration gen;
    FabricId fabric;
    std::vector<DeviceId> members;      // sorted set (unique).
    std::vector<SwitchId> switches;     // sorted set (unique).
    PartitionState state{PartitionState::ACTIVE};
    WorkerId worker;
    bool workerKnown{false};
    WorkerBootId workerBoot{0};
    Provenance provenance{Provenance::UNSUPPORTED};
    EvidenceSource source{EvidenceSource::UNKNOWN};
    TopologyGeneration topologyGen;
};

} // namespace nvswitch_fabric
