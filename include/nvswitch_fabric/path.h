// NVSwitch Fabric - switch-mediated paths.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include "nvswitch_fabric/identity.h"
#include "nvswitch_fabric/generation.h"
#include "nvswitch_fabric/provenance.h"
#include "nvswitch_fabric/state.h"

namespace nvswitch_fabric {

// A single traversal decision through one switch: which port we came in on and
// which port we leave by, plus the links used.
struct PathHop {
    SwitchId sw;                    // the switch being traversed.
    PortId inPort;                  // port on sw used to arrive.
    PortId outPort;                 // port on sw used to leave.
    bool exitToDevice{false};       // true when outPort attaches to a device.
    bool exitToSwitch{false};       // true when the next hop is another switch.

    friend bool operator==(const PathHop&, const PathHop&) = default;
    auto operator<=>(const PathHop&) const = default;
};

// A switch-mediated path from source to destination.
struct FabricPath {
    PathId id;
    DeviceId source;
    DeviceId destination;
    std::vector<PathHop> hops;      // ordered switch traversal.
    std::vector<LinkId> links;      // ordered links used.
    std::vector<SwitchId> switchSequence;
    std::vector<std::pair<SwitchId, SwitchGeneration>> switchGens;   // captured per-switch gen
    std::vector<std::pair<PortId, PortGeneration>> portGens;         // captured per-port gen
    TopologyGeneration topologyGen;
    PartitionGeneration partitionGen;
    SwitchGeneration switchGen;     // minimum switch generation along the path.
    PortGeneration portGen;         // minimum port generation along the path.
    bool capable{false};
    EntityState state{EntityState::UNKNOWN};
    Provenance provenance{Provenance::UNSUPPORTED};
    EvidenceSource evidenceSource{EvidenceSource::UNKNOWN};

    // Canonical key used for deterministic tie-breaking and dedup.
    std::string canonicalKey() const;
    friend bool operator==(const FabricPath&, const FabricPath&) = default;
};

} // namespace nvswitch_fabric
