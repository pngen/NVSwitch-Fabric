// NVSwitch Fabric - operational/observation state enums.
#pragma once
#include <cstdint>

namespace nvswitch_fabric {

enum class EntityState : uint8_t {
    UNKNOWN = 0,
    PRESENT = 1,
    ABSENT = 2,
    SUPPORTED = 3,
    UNSUPPORTED = 4,
    UP = 5,
    DOWN = 6,
    DEGRADED = 7,
    DISABLED = 8,
    UNREACHABLE = 9,
    PARTITIONED = 10,
    STALE = 11,
    REVALIDATION_REQUIRED = 12,
    INCOMPATIBLE = 13,
};

enum class PartitionState : uint8_t {
    ACTIVE = 0,
    INACTIVE = 1,
    RECONFIGURING = 2,
    REVALIDATION_REQUIRED = 3,
    STALE = 4,
    UNSUPPORTED = 5,
};

enum class Reachability : uint8_t {
    REACHABLE = 0,
    REACHABLE_DEGRADED = 1,
    UNREACHABLE = 2,
    PARTITION_BLOCKED = 3,
    SWITCH_DOWN = 4,
    PORT_DOWN = 5,
    ENDPOINT_STALE = 6,
    TOPOLOGY_STALE = 7,
    REVALIDATION_REQUIRED = 8,
    INSUFFICIENT_EVIDENCE = 9,
    UNSUPPORTED = 10,
};

enum class FabricHealth : uint8_t {
    HEALTHY = 0,
    DEGRADED = 1,
    PARTIALLY_REACHABLE = 2,
    PARTITIONED = 3,
    SWITCH_FAILURE = 4,
    PORT_FAILURE = 5,
    ENDPOINT_FAILURE = 6,
    REVALIDATION_REQUIRED = 7,
    INSUFFICIENT_EVIDENCE = 8,
    UNSUPPORTED = 9,
};

enum class CongestionEvidence : uint8_t {
    NO_CONGESTION_EVIDENCE = 0,
    CONGESTION_SUSPECTED = 1,
    CONGESTION_CONFIRMED = 2,
    DEGRADED = 3,
    LINK_FAILURE = 4,
    SWITCH_FAILURE = 5,
    INSUFFICIENT_EVIDENCE = 6,
    UNSUPPORTED = 7,
};

enum class RouteOutcome : uint8_t {
    ROUTE_ALLOWED = 0,
    ROUTE_ALLOWED_DEGRADED = 1,
    NO_ROUTE = 2,
    PARTITION_BLOCKED = 3,
    SWITCH_DOWN = 4,
    PORT_DOWN = 5,
    STALE_TOPOLOGY = 6,
    STALE_PARTITION = 7,
    STALE_SWITCH = 8,
    REVALIDATION_REQUIRED = 9,
    INSUFFICIENT_EVIDENCE = 10,
    POLICY_REJECTED = 11,
    UNSUPPORTED = 12,
};

inline const char* toString(EntityState s) noexcept {
    switch (s) { case EntityState::UNKNOWN:return "UNKNOWN"; case EntityState::PRESENT:return "PRESENT";
        case EntityState::ABSENT:return "ABSENT"; case EntityState::SUPPORTED:return "SUPPORTED";
        case EntityState::UNSUPPORTED:return "UNSUPPORTED"; case EntityState::UP:return "UP";
        case EntityState::DOWN:return "DOWN"; case EntityState::DEGRADED:return "DEGRADED";
        case EntityState::DISABLED:return "DISABLED"; case EntityState::UNREACHABLE:return "UNREACHABLE";
        case EntityState::PARTITIONED:return "PARTITIONED"; case EntityState::STALE:return "STALE";
        case EntityState::REVALIDATION_REQUIRED:return "REVALIDATION_REQUIRED";
        case EntityState::INCOMPATIBLE:return "INCOMPATIBLE"; } return "UNKNOWN";
}
inline const char* toString(PartitionState s) noexcept {
    switch (s) { case PartitionState::ACTIVE:return "ACTIVE"; case PartitionState::INACTIVE:return "INACTIVE";
        case PartitionState::RECONFIGURING:return "RECONFIGURING"; case PartitionState::REVALIDATION_REQUIRED:return "REVALIDATION_REQUIRED";
        case PartitionState::STALE:return "STALE"; case PartitionState::UNSUPPORTED:return "UNSUPPORTED"; } return "ACTIVE";
}
inline const char* toString(Reachability r) noexcept {
    switch (r) { case Reachability::REACHABLE:return "REACHABLE"; case Reachability::REACHABLE_DEGRADED:return "REACHABLE_DEGRADED";
        case Reachability::UNREACHABLE:return "UNREACHABLE"; case Reachability::PARTITION_BLOCKED:return "PARTITION_BLOCKED";
        case Reachability::SWITCH_DOWN:return "SWITCH_DOWN"; case Reachability::PORT_DOWN:return "PORT_DOWN";
        case Reachability::ENDPOINT_STALE:return "ENDPOINT_STALE"; case Reachability::TOPOLOGY_STALE:return "TOPOLOGY_STALE";
        case Reachability::REVALIDATION_REQUIRED:return "REVALIDATION_REQUIRED"; case Reachability::INSUFFICIENT_EVIDENCE:return "INSUFFICIENT_EVIDENCE";
        case Reachability::UNSUPPORTED:return "UNSUPPORTED"; } return "UNREACHABLE";
}
inline const char* toString(FabricHealth h) noexcept {
    switch (h) { case FabricHealth::HEALTHY:return "HEALTHY"; case FabricHealth::DEGRADED:return "DEGRADED";
        case FabricHealth::PARTIALLY_REACHABLE:return "PARTIALLY_REACHABLE"; case FabricHealth::PARTITIONED:return "PARTITIONED";
        case FabricHealth::SWITCH_FAILURE:return "SWITCH_FAILURE"; case FabricHealth::PORT_FAILURE:return "PORT_FAILURE";
        case FabricHealth::ENDPOINT_FAILURE:return "ENDPOINT_FAILURE"; case FabricHealth::REVALIDATION_REQUIRED:return "REVALIDATION_REQUIRED";
        case FabricHealth::INSUFFICIENT_EVIDENCE:return "INSUFFICIENT_EVIDENCE"; case FabricHealth::UNSUPPORTED:return "UNSUPPORTED"; } return "UNSUPPORTED";
}
inline const char* toString(CongestionEvidence c) noexcept {
    switch (c) { case CongestionEvidence::NO_CONGESTION_EVIDENCE:return "NO_CONGESTION_EVIDENCE";
        case CongestionEvidence::CONGESTION_SUSPECTED:return "CONGESTION_SUSPECTED"; case CongestionEvidence::CONGESTION_CONFIRMED:return "CONGESTION_CONFIRMED";
        case CongestionEvidence::DEGRADED:return "DEGRADED"; case CongestionEvidence::LINK_FAILURE:return "LINK_FAILURE";
        case CongestionEvidence::SWITCH_FAILURE:return "SWITCH_FAILURE"; case CongestionEvidence::INSUFFICIENT_EVIDENCE:return "INSUFFICIENT_EVIDENCE";
        case CongestionEvidence::UNSUPPORTED:return "UNSUPPORTED"; } return "UNSUPPORTED";
}
inline const char* toString(RouteOutcome o) noexcept {
    switch (o) { case RouteOutcome::ROUTE_ALLOWED:return "ROUTE_ALLOWED"; case RouteOutcome::ROUTE_ALLOWED_DEGRADED:return "ROUTE_ALLOWED_DEGRADED";
        case RouteOutcome::NO_ROUTE:return "NO_ROUTE"; case RouteOutcome::PARTITION_BLOCKED:return "PARTITION_BLOCKED";
        case RouteOutcome::SWITCH_DOWN:return "SWITCH_DOWN"; case RouteOutcome::PORT_DOWN:return "PORT_DOWN";
        case RouteOutcome::STALE_TOPOLOGY:return "STALE_TOPOLOGY"; case RouteOutcome::STALE_PARTITION:return "STALE_PARTITION";
        case RouteOutcome::STALE_SWITCH:return "STALE_SWITCH"; case RouteOutcome::REVALIDATION_REQUIRED:return "REVALIDATION_REQUIRED";
        case RouteOutcome::INSUFFICIENT_EVIDENCE:return "INSUFFICIENT_EVIDENCE"; case RouteOutcome::POLICY_REJECTED:return "POLICY_REJECTED";
        case RouteOutcome::UNSUPPORTED:return "UNSUPPORTED"; } return "UNSUPPORTED";
}

} // namespace nvswitch_fabric
