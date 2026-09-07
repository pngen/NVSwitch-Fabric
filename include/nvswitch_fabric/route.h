// NVSwitch Fabric - route governance.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <set>
#include <optional>
#include <utility>
#include "nvswitch_fabric/identity.h"
#include "nvswitch_fabric/generation.h"
#include "nvswitch_fabric/limits.h"
#include "nvswitch_fabric/path.h"
#include "nvswitch_fabric/state.h"
#include "nvswitch_fabric/error.h"
#include "nvswitch_fabric/evidence.h"

namespace nvswitch_fabric {

class FabricTopology;


// Named ranking factors (deterministic order/weights).
enum class RouteFactor : uint8_t {
    FEWER_SWITCHES = 0,
    FEWER_HOPS = 1,
    DIRECTNESS = 2,
    REDUNDANCY = 3,
    MEASURED_THROUGHPUT = 4,
    MEASURED_LATENCY = 5,
    ACTIVE_LINK_COUNT = 6,
    DEGRADATION = 7,
    CONGESTION_EVIDENCE = 8,
    FRESHNESS = 9,
    CONFIDENCE = 10,
    FAILURE_DOMAIN_DIVERSITY = 11,
    POLICY_PREFERENCE = 12,
};
inline const char* toString(RouteFactor f) noexcept {
    switch (f) {
        case RouteFactor::FEWER_SWITCHES:return "FEWER_SWITCHES";
        case RouteFactor::FEWER_HOPS:return "FEWER_HOPS";
        case RouteFactor::DIRECTNESS:return "DIRECTNESS";
        case RouteFactor::REDUNDANCY:return "REDUNDANCY";
        case RouteFactor::MEASURED_THROUGHPUT:return "MEASURED_THROUGHPUT";
        case RouteFactor::MEASURED_LATENCY:return "MEASURED_LATENCY";
        case RouteFactor::ACTIVE_LINK_COUNT:return "ACTIVE_LINK_COUNT";
        case RouteFactor::DEGRADATION:return "DEGRADATION";
        case RouteFactor::CONGESTION_EVIDENCE:return "CONGESTION_EVIDENCE";
        case RouteFactor::FRESHNESS:return "FRESHNESS";
        case RouteFactor::CONFIDENCE:return "CONFIDENCE";
        case RouteFactor::FAILURE_DOMAIN_DIVERSITY:return "FAILURE_DOMAIN_DIVERSITY";
        case RouteFactor::POLICY_PREFERENCE:return "POLICY_PREFERENCE";
    }
    return "UNKNOWN";
}

// A hard eligibility filter that must pass before any scoring.
struct RoutePolicy {
    PolicyId id;
    bool requireSufficientsSwitches{true};
    bool requireSufficientsPorts{true};
    bool requireNonDegradedPath{false};
    bool insidePartition{true};
    uint64_t maxAllowedSwitches{limits::kMaxSwitches};
    uint64_t maxAllowedHops{limits::kMaxPathDepth};
    double requiredMinRelativeBandwidth{0.0};
    bool preferRedundantPath{false};
    bool requireFreshEvidence{true};
    uint64_t maxStalenessNanos{0};       // 0 => any replay/old is stale.

    // Ordered factor weights; higher weight ranks first, then deterministic
    // tie-breaking on the canonical path key. Weights are applied in the
    // declared order (deterministic even for equal weights).
    std::vector<std::pair<RouteFactor, double>> factors{
        {RouteFactor::FEWER_SWITCHES, 1.0},
        {RouteFactor::FEWER_HOPS, 1.0},
        {RouteFactor::DIRECTNESS, 1.0},
        {RouteFactor::DEGRADATION, 1.0},
        {RouteFactor::CONGESTION_EVIDENCE, 1.0},
    };
};

// A rejected candidate with its explicit reason.
struct RejectedCandidate {
    std::string canonicalKey;
    std::string reason;
    RouteOutcome outcome{RouteOutcome::NO_ROUTE};
};

struct RouteDecision {
    RouteDecisionId id;
    RouteGeneration gen;
    DeviceId source;
    DeviceId destination;
    bool sourceKnown{false};
    bool destinationKnown{false};
    FabricGeneration fabricGen;
    TopologyGeneration topologyGen;
    PartitionGeneration partitionGen;
    PartitionId partition;
    bool partitionKnown{false};
    RouteOutcome outcome{RouteOutcome::NO_ROUTE};
    std::string outcomeReason;
    bool authoritative{false};              // false once superseded/restart.
    std::vector<FabricPath> candidatePaths; // eligible candidates, ranked.
    std::vector<RejectedCandidate> rejected;
    std::optional<FabricPath> selected;
    PolicyId policy;
    RoutePolicy policySnapshot;
    std::vector<std::pair<RouteFactor,double>> factorScores; // selected path scores.
    std::string explanation;
    TopologySnapshotId snapshotId;          // topology snapshot that produced it (empty if none).
    ObservationId observation;
    bool observationKnown{false};
    WorkerId worker;
    bool workerKnown{false};
    WorkerBootId workerBoot{0};
    CoordinatorEpoch epoch{0};
    uint64_t createdAtNanos{0};
};

// Compute a route decision deterministically from a topology snapshot.
// Always returns a decision (never a hard error for ordinary queries); the
// outcome encodes the reason when a route is not eligible. `id`/`gen`/
// `snapshotId` are supplied by the registry so ids stay globally unique.
Result<RouteDecision> computeRoute(const FabricTopology& topo, DeviceId src, DeviceId dst,
                                   const RoutePolicy& policy,
                                   const std::vector<Measurement>* measurements,
                                   RouteDecisionId id, RouteGeneration gen,
                                   const TopologySnapshotId& snapshotId, uint64_t now,
                                   CoordinatorEpoch epoch);

// Whether the generations captured in an existing decision are still current
// w.r.t. `topo`. If any relevant entity advanced or a referenced switch/port/
// partition went stale, the decision is no longer authoritative.
bool routeDecisionStillAuthoritative(const RouteDecision& d, const FabricTopology& topo);

} // namespace nvswitch_fabric
