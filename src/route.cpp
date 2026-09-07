// NVSwitch Fabric - deterministic route ranking + decision.
#include "nvswitch_fabric/route.h"
#include "nvswitch_fabric/topology.h"
#include "nvswitch_fabric/evidence.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace nvswitch_fabric {

namespace {

bool currentState(EntityState s) {
    switch (s) {
        case EntityState::UP:
        case EntityState::DEGRADED:
        case EntityState::PRESENT:
        case EntityState::SUPPORTED:
            return true;
        case EntityState::STALE:
        case EntityState::REVALIDATION_REQUIRED:
        case EntityState::UNKNOWN:
        case EntityState::DOWN:
        case EntityState::DISABLED:
        case EntityState::UNREACHABLE:
        case EntityState::PARTITIONED:
        case EntityState::ABSENT:
        case EntityState::INCOMPATIBLE:
        case EntityState::UNSUPPORTED:
            return false;
    }
    return false;
}
bool usableState(EntityState s) {
    return s == EntityState::UP || s == EntityState::DEGRADED;
}

// Gather the best measurement for a src->dst pair (highest throughput, then
// lowest latency).
struct BestMeasurement {
    double bestThroughput = 0.0;
    double bestLatencyUs = std::numeric_limits<double>::max();
    bool has = false;
};

BestMeasurement bestFor(const std::vector<Measurement>* measurements, DeviceId src, DeviceId dst) {
    BestMeasurement b;
    if (!measurements) return b;
    for (const auto& m : *measurements) {
        if (!m.sourceKnown || !m.destinationKnown) continue;
        if (m.source != src || m.destination != dst) continue;
        if (!m.matched) continue;                       // only fresh/current measurements.
        if (m.kind == MeasurementKind::UNSUPPORTED) continue;
        b.has = true;
        double t = m.bytesPerSec();
        if (t > b.bestThroughput) b.bestThroughput = t;
        double l = m.latencyUsPerOp();
        if (l < b.bestLatencyUs) b.bestLatencyUs = l;
    }
    return b;
}

// Compute a scalar for a candidate path, where LOWER is always better.
double factorValue(RouteFactor f, const FabricPath& p, const FabricTopology& topo,
                   const BestMeasurement& meas, const RoutePolicy& policy) {
    (void)policy;
    switch (f) {
        case RouteFactor::FEWER_SWITCHES: return (double)p.hops.size();
        case RouteFactor::FEWER_HOPS:     return (double)p.hops.size();
        case RouteFactor::DIRECTNESS:     return p.hops.empty() ? 0.0 : (double)p.hops.size();
        case RouteFactor::REDUNDANCY: {
            // Higher redundancy is better; we prefer fewer switches for
            // diversity, so value a shorter path. Lower is better => negate.
            return -(double)p.hops.size();
        }
        case RouteFactor::MEASURED_THROUGHPUT: {
            // Higher throughput better => negate. Unknown => large (worst).
            if (!meas.has || meas.bestThroughput <= 0.0) return std::numeric_limits<double>::max();
            return -meas.bestThroughput;
        }
        case RouteFactor::MEASURED_LATENCY: {
            if (!meas.has || meas.bestLatencyUs >= std::numeric_limits<double>::max())
                return std::numeric_limits<double>::max();
            return meas.bestLatencyUs;
        }
        case RouteFactor::ACTIVE_LINK_COUNT: {
            // Prefer more active links => negate count.
            double active = 0;
            for (auto lid : p.links) { auto l = topo.lookupLink(lid); if (l) active += l->state == EntityState::UP ? 1.0 : 0.5; }
            return -active;
        }
        case RouteFactor::DEGRADATION: {
            // 0 = not degraded, 1 = degraded. Lower is better. Unowned => 0.
            // Determine from path state.
            return p.state == EntityState::DEGRADED ? 1.0 : 0.0;
        }
        case RouteFactor::CONGESTION_EVIDENCE: {
            // Lower congestion is better; we use path state as a proxy plus
            // per-link degradation flags from the topology.
            double c = 0.0;
            for (auto lid : p.links) { auto l = topo.lookupLink(lid); if (l && l->degraded) c += 1.0; }
            return c;
        }
        case RouteFactor::FRESHNESS: {
            // Paths on a current snapshot are fresh; older generations worse.
            return 0.0;
        }
        case RouteFactor::CONFIDENCE: return 0.0;
        case RouteFactor::FAILURE_DOMAIN_DIVERSITY: {
            // A path with fewer switches shares fewer common failure domains; but
            // diversity to an *alternate* single path is best expressed by an
            // explicit penalty for shared switches in the selection logic. Here
            // lower switch count is preferred.
            return (double)p.hops.size();
        }
        case RouteFactor::POLICY_PREFERENCE: return 0.0;
    }
    return 0.0;
}

} // namespace

Result<RouteDecision> computeRoute(const FabricTopology& topo, DeviceId src, DeviceId dst,
                                   const RoutePolicy& policy,
                                   const std::vector<Measurement>* measurements,
                                   RouteDecisionId id, RouteGeneration gen,
                                   const TopologySnapshotId& snapshotId, uint64_t now,
                                   CoordinatorEpoch epoch) {
    RouteDecision d;
    d.epoch = epoch;
    d.id = id; d.gen = gen;
    d.source = src; d.destination = dst;
    d.sourceKnown = true; d.destinationKnown = true;
    d.fabricGen = topo.fabricGen();
    d.topologyGen = topo.topologyGen();
    d.policy = policy.id;
    d.snapshotId = snapshotId;
    d.createdAtNanos = now;
    d.policySnapshot = policy;

    auto s = topo.lookupDevice(src);
    auto dd = topo.lookupDevice(dst);
    if (!s || !dd) { d.outcome = RouteOutcome::NO_ROUTE; d.outcomeReason = "source or destination not in topology"; return Ok(std::move(d)); }
    if (!s->present || !dd->present) { d.outcome = RouteOutcome::NO_ROUTE; d.outcomeReason = "endpoint absent"; return Ok(std::move(d)); }
    if (src == dst) { d.outcome = RouteOutcome::NO_ROUTE; d.outcomeReason = "same endpoint"; return Ok(std::move(d)); }
    if (!currentState(s->state)) { d.outcome = RouteOutcome::REVALIDATION_REQUIRED; d.outcomeReason = "source not current"; return Ok(std::move(d)); }
    if (!currentState(dd->state)) { d.outcome = RouteOutcome::REVALIDATION_REQUIRED; d.outcomeReason = "destination not current"; return Ok(std::move(d)); }
    if (s->state == EntityState::UNSUPPORTED || dd->state == EntityState::UNSUPPORTED) {
        d.outcome = RouteOutcome::UNSUPPORTED; d.outcomeReason = "endpoint unsupported"; return Ok(std::move(d));
    }

    // Partition gating.
    std::optional<FabricPartition> part;
    std::map<PartitionId, FabricPartition> activePartitions;
    for (const auto& [pid, p] : topo.partitions()) {
        if (p.state == PartitionState::STALE || p.state == PartitionState::REVALIDATION_REQUIRED)
            continue;
        activePartitions[pid] = p;
        if (std::find(p.members.begin(), p.members.end(), src) != p.members.end() &&
            std::find(p.members.begin(), p.members.end(), dst) != p.members.end()) {
            part = p;
        }
    }
    bool anyStalePartition = false;
    for (const auto& [pid, p] : topo.partitions()) {
        if (p.state == PartitionState::STALE) anyStalePartition = true;
    }
    if (!activePartitions.empty() && !part) {
        // Partitions exist but no single active partition contains both endpoints.
        if (policy.insidePartition) {
            d.outcome = anyStalePartition ? RouteOutcome::STALE_PARTITION : RouteOutcome::PARTITION_BLOCKED;
            d.outcomeReason = "no active partition contains both endpoints";
            return Ok(std::move(d));
        }
    } else if (part) {
        d.partition = part->id;
        d.partitionKnown = true;
        d.partitionGen = part->gen;
    }

    // Enumerate candidate paths.
    TraversalConfig cfg;
    cfg.maxPaths = (uint32_t)std::min<uint64_t>(policy.maxAllowedHops + 1, limits::kMaxCandidatePaths);
    cfg.maxDepth = (uint32_t)std::min<uint64_t>(policy.maxAllowedHops, limits::kMaxPathDepth);
    cfg.allowSwitchSwitching = true;
    auto raw = topo.enumeratePaths(src, dst, cfg);
    if (raw.empty()) {
        // Determine the reason.
        auto rr = topo.deviceReachability(src, dst);
        switch (rr) {
            case Reachability::PARTITION_BLOCKED: d.outcome = RouteOutcome::PARTITION_BLOCKED; d.outcomeReason="partition blocked"; return Ok(std::move(d));
            case Reachability::SWITCH_DOWN: d.outcome = RouteOutcome::SWITCH_DOWN; d.outcomeReason="switch down"; return Ok(std::move(d));
            case Reachability::PORT_DOWN: d.outcome = RouteOutcome::PORT_DOWN; d.outcomeReason="port down"; return Ok(std::move(d));
            case Reachability::REVALIDATION_REQUIRED: d.outcome = RouteOutcome::REVALIDATION_REQUIRED; d.outcomeReason="revalidation required"; return Ok(std::move(d));
            case Reachability::TOPOLOGY_STALE: d.outcome = RouteOutcome::STALE_TOPOLOGY; d.outcomeReason="stale topology"; return Ok(std::move(d));
            case Reachability::ENDPOINT_STALE: d.outcome = RouteOutcome::REVALIDATION_REQUIRED; d.outcomeReason="endpoint stale"; return Ok(std::move(d));
            case Reachability::UNSUPPORTED: d.outcome = RouteOutcome::UNSUPPORTED; d.outcomeReason="unsupported"; return Ok(std::move(d));
            case Reachability::INSUFFICIENT_EVIDENCE: d.outcome = RouteOutcome::INSUFFICIENT_EVIDENCE; d.outcomeReason="insufficient evidence"; return Ok(std::move(d));
            default: d.outcome = RouteOutcome::NO_ROUTE; d.outcomeReason="no path"; return Ok(std::move(d));
        }
    }

    // Hard per-path filters.
    std::vector<FabricPath> eligible;
    for (auto& p : raw) {
        bool ok = true;
        // All switches in the path must be current + usable.
        for (auto sw : p.switchSequence) {
            auto rec = topo.lookupSwitch(sw);
            if (!rec) { ok = false; break; }
            if (!currentState(rec->state)) { ok = false; break; }
        }
        if (!ok) { p.capable = false; continue; }
        // All ports usable.
        for (auto& h : p.hops) {
            if (!h.inPort.isUnknown()) {
                auto pr = topo.lookupPort(h.inPort);
                if (!pr || pr->state == EntityState::STALE || pr->state == EntityState::REVALIDATION_REQUIRED) { ok = false; break; }
            }
            auto pr = topo.lookupPort(h.outPort);
            if (!pr || pr->state == EntityState::STALE || pr->state == EntityState::REVALIDATION_REQUIRED) { ok = false; break; }
        }
        if (!ok) { p.capable = false; continue; }
        // Freshness.
        bool degraded = p.state == EntityState::DEGRADED;
        // Ports/links all current.
        for (auto lid : p.links) {
            auto l = topo.lookupLink(lid);
            if (!l) { ok = false; break; }
            if (l->state == EntityState::STALE || l->state == EntityState::REVALIDATION_REQUIRED) { ok = false; break; }
        }
        if (!ok) { p.capable = false; continue; }
        if (policy.requireNonDegradedPath && degraded) { p.capable = false; continue; }
        // Min bandwidth requirement.
        if (policy.requiredMinRelativeBandwidth > 0.0) {
            // (No authoritative nominal bandwidth on the path; treat as pass only
            // if non-degraded.)
            if (degraded) { p.capable = false; continue; }
        }
        p.capable = true;
        p.switchGens.clear();
        for (auto sw : p.switchSequence) { if (auto rec = topo.lookupSwitch(sw); rec) p.switchGens.emplace_back(sw, rec->gen); }
        p.portGens.clear();
        for (const auto& h : p.hops) {
            if (!h.inPort.isUnknown()) { if (auto pr = topo.lookupPort(h.inPort); pr) p.portGens.emplace_back(h.inPort, pr->gen); }
            if (auto pr = topo.lookupPort(h.outPort); pr) p.portGens.emplace_back(h.outPort, pr->gen);
        }
        eligible.push_back(std::move(p));
    }

    // Recompute min generations across eligible paths.
    for (auto& p : eligible) {
        uint64_t msw = UINT64_MAX; bool anySw = false;
        for (auto sw : p.switchSequence) { if (auto rec = topo.lookupSwitch(sw); rec) { msw = std::min(msw, rec->gen.get()); anySw = true; } }
        if (anySw) p.switchGen = SwitchGeneration{msw};
        uint64_t mpr = UINT64_MAX; bool anyPort = false;
        for (const auto& h : p.hops) {
            if (!h.inPort.isUnknown()) { if (auto pr = topo.lookupPort(h.inPort); pr) { mpr = std::min(mpr, pr->gen.get()); anyPort = true; } }
            if (auto pr = topo.lookupPort(h.outPort); pr) { mpr = std::min(mpr, pr->gen.get()); anyPort = true; }
        }
        if (anyPort) p.portGen = PortGeneration{mpr};
    }

    if (eligible.empty()) {
        bool anyStaleSwitch = false;
        for (auto& [swid, rec] : topo.switches()) if (rec.state==EntityState::STALE || rec.state==EntityState::REVALIDATION_REQUIRED) anyStaleSwitch = true;
        d.outcome = anyStaleSwitch ? RouteOutcome::STALE_SWITCH : RouteOutcome::NO_ROUTE;
        d.outcomeReason = "no eligible path";
        return Ok(std::move(d));
    }

    // Rank eligible candidates deterministically: lexicographic factor compare
    // then canonicalKey tie-break.
    auto meas = bestFor(measurements, src, dst);
    std::stable_sort(eligible.begin(), eligible.end(), [&](const FabricPath& a, const FabricPath& b) {
        for (const auto& [fac, w] : policy.factors) {
            double va = factorValue(fac, a, topo, meas, policy);
            double vb = factorValue(fac, b, topo, meas, policy);
            if (va < vb) return true;
            if (vb < va) return false;
        }
        return a.canonicalKey() < b.canonicalKey();
    });

    const FabricPath& best = eligible.front();
    d.candidatePaths = eligible;
    d.selected = best;
    d.factorScores.clear();
    for (const auto& [fac, w] : policy.factors)
        d.factorScores.emplace_back(fac, factorValue(fac, best, topo, meas, policy));
    bool degraded = best.state == EntityState::DEGRADED;
    d.outcome = degraded ? RouteOutcome::ROUTE_ALLOWED_DEGRADED : RouteOutcome::ROUTE_ALLOWED;
    d.authoritative = true;
    std::string expl = "selected " + best.canonicalKey();
    expl += " after applying " + std::to_string(policy.factors.size()) + " named factors";
    d.explanation = expl;
    return Ok(std::move(d));
}

bool routeDecisionStillAuthoritative(const RouteDecision& d, const FabricTopology& topo) {
    // Only a previously-allowed decision can be authoritative at all.
    if (d.outcome != RouteOutcome::ROUTE_ALLOWED &&
        d.outcome != RouteOutcome::ROUTE_ALLOWED_DEGRADED)
        return false;
    // A decision produced under a prior coordinator epoch cannot remain
    // authoritative after the coordinator restarted into a new epoch.
    if (d.epoch != topo.epoch()) return false;

    // Endpoints must still be current.
    auto s = topo.lookupDevice(d.source);
    auto dd = topo.lookupDevice(d.destination);
    if (!s || !dd) return false;
    if (!usableState(s->state) && s->state != EntityState::PRESENT) return false;
    if (!usableState(dd->state) && dd->state != EntityState::PRESENT) return false;

    // Partition must be unchanged.
    if (d.partitionKnown) {
        auto part = topo.lookupPartition(d.partition);
        if (!part) return false;
        if (part->state == PartitionState::STALE || part->state == PartitionState::REVALIDATION_REQUIRED) return false;
        if (part->gen.get() != d.partitionGen.get()) return false;
    }

    if (d.selected) {
        const auto& p = *d.selected;
        for (const auto& [sw, g] : p.switchGens) {
            auto rec = topo.lookupSwitch(sw);
            if (!rec) return false;
            if (!usableState(rec->state)) return false;
            if (rec->gen.get() != g.get()) return false;
        }
        for (const auto& [pid, g] : p.portGens) {
            auto pr = topo.lookupPort(pid);
            if (!pr) return false;
            if (!usableState(pr->state)) return false;
            if (pr->gen.get() != g.get()) return false;
        }
    }
    return true;
}

} // namespace nvswitch_fabric
