// NVSwitch Fabric - synthetic switched-fabric backend & scenario builders.
// Every record produced here is visibly SYNTHETIC (Provenance::SYNTHETIC) and
// sourced from SYNTHETIC_FIXTURE. Nothing synthetic is ever presented as
// hardware proof.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "nvswitch_fabric/backend.h"
#include "nvswitch_fabric/records.h"
#include "nvswitch_fabric/fabric_registry.h"

namespace nvswitch_fabric {

// Named synthetic scenarios exercising the switched-fabric domain.
enum class ScenarioType : uint8_t {
    EMPTY = 0,
    SINGLE_SWITCH_TWO_GPU = 1,
    SINGLE_SWITCH_MANY_GPU = 2,
    TWO_SWITCH_REDUNDANT = 3,
    TWO_SWITCH_LINEAR = 4,
    DENSE_MULTI_SWITCH = 5,
    PARTITION_SPLIT = 6,
    DEGRADED_PORT = 7,
    PARALLEL_LINKS = 8,
};

inline const char* toString(ScenarioType s) noexcept {
    switch (s) {
        case ScenarioType::EMPTY: return "EMPTY";
        case ScenarioType::SINGLE_SWITCH_TWO_GPU: return "SINGLE_SWITCH_TWO_GPU";
        case ScenarioType::SINGLE_SWITCH_MANY_GPU: return "SINGLE_SWITCH_MANY_GPU";
        case ScenarioType::TWO_SWITCH_REDUNDANT: return "TWO_SWITCH_REDUNDANT";
        case ScenarioType::TWO_SWITCH_LINEAR: return "TWO_SWITCH_LINEAR";
        case ScenarioType::DENSE_MULTI_SWITCH: return "DENSE_MULTI_SWITCH";
        case ScenarioType::PARTITION_SPLIT: return "PARTITION_SPLIT";
        case ScenarioType::DEGRADED_PORT: return "DEGRADED_PORT";
        case ScenarioType::PARALLEL_LINKS: return "PARALLEL_LINKS";
    }
    return "EMPTY";
}

// Record builders (all synthetic provenance / source).
DeviceRecord makeSyntheticDevice(DeviceId id, const std::string& gen, EntityState st);
SwitchRecord makeSyntheticSwitch(SwitchId id, const std::string& generation, uint32_t portCount);
SwitchPortRecord makeSyntheticPort(SwitchId sw, uint32_t index, const LinkEnd& attached);
LinkRecord makeSyntheticLink(LinkId id, SwitchId root, PortId port, const LinkEnd& other);
FabricPartition makeSyntheticPartition(PartitionId id, std::vector<DeviceId> members,
                                       std::vector<SwitchId> switches, PartitionState st);

// Builds the base records (scan) for a synthetic scenario.
Backend::Scan buildSyntheticScan(ScenarioType type);

// A synthetic backend that emits the chosen scenario's records.
class SyntheticBackend : public Backend {
public:
    explicit SyntheticBackend(ScenarioType type = ScenarioType::SINGLE_SWITCH_TWO_GPU);
    Result<void> initialize() override;
    void shutdown() noexcept override;
    BackendCapabilities capabilities() const override;
    Result<Backend::Scan> scan() override;

private:
    ScenarioType type_;
    Backend::Scan scan_;
};

// Applies a scenario's records to a registry transactionally and returns the
// resulting snapshot (or the error).
Result<FabricRegistry::Snapshot> applyScenarioToRegistry(FabricRegistry& reg, ScenarioType type);

} // namespace nvswitch_fabric
