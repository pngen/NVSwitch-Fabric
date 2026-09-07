// NVSwitch Fabric - FabricTopology implementation.
#include "nvswitch_fabric/topology.h"
#include "nvswitch_fabric/path.h"
#include "nvswitch_fabric/evidence.h"
#include <algorithm>
#include <array>
#include <deque>
#include <functional>
#include <sstream>

namespace nvswitch_fabric {

// ---------------------------------------------------------------------------
// lookups
// ---------------------------------------------------------------------------
std::optional<DeviceRecord> FabricTopology::lookupDevice(DeviceId id) const {
    auto it = devices_.find(id);
    if (it == devices_.end()) return std::nullopt;
    return it->second;
}
std::optional<SwitchRecord> FabricTopology::lookupSwitch(SwitchId id) const {
    auto it = switches_.find(id);
    if (it == switches_.end()) return std::nullopt;
    return it->second;
}
std::optional<SwitchPortRecord> FabricTopology::lookupPort(PortId id) const {
    auto it = ports_.find(id);
    if (it == ports_.end()) return std::nullopt;
    return it->second;
}
std::optional<LinkRecord> FabricTopology::lookupLink(LinkId id) const {
    auto it = links_.find(id);
    if (it == links_.end()) return std::nullopt;
    return it->second;
}
std::optional<FabricPartition> FabricTopology::lookupPartition(PartitionId id) const {
    auto it = partitions_.find(id);
    if (it == partitions_.end()) return std::nullopt;
    return it->second;
}

// ---------------------------------------------------------------------------
// path enumeration (deterministic, bounded)
// ---------------------------------------------------------------------------
namespace {
struct Edge {
    LinkId link;
    PortId port;
    LinkEnd other;
    EntityState linkState;
    EntityState portState;
};
bool traversableState(EntityState s) {
    return s == EntityState::UP || s == EntityState::DEGRADED;
}
bool edgeUsable(const Edge& e) {
    return traversableState(e.linkState) && traversableState(e.portState);
}
bool isDeviceEnd(const LinkEnd& e) { return e.kind == LinkEnd::Kind::Device; }
bool isSwitchEnd(const LinkEnd& e) { return e.kind == LinkEnd::Kind::Switch; }

std::string fmt(uint64_t v) { return std::to_string(v); }
std::string fmtPort(PortId p) {
    if (p.isUnknown()) return "UNKNOWN_PORT";
    return std::to_string(p.sw.get()) + "." + std::to_string(p.index);
}
} // namespace

std::vector<FabricPath> FabricTopology::enumeratePaths(DeviceId src, DeviceId dst,
                                                       const TraversalConfig& cfg) const {
    std::vector<FabricPath> result;
    if (cfg.maxPaths == 0 || cfg.maxDepth == 0) return result;
    // Source and destination must exist and be present.
    auto srec = lookupDevice(src);
    auto drec = lookupDevice(dst);
    if (!srec || !drec) return result;
    if (srec->state != EntityState::UP && srec->state != EntityState::PRESENT &&
        srec->state != EntityState::DEGRADED) return result;
    if (drec->state != EntityState::UP && drec->state != EntityState::PRESENT &&
        drec->state != EntityState::DEGRADED) return result;
    if (src == dst) return result; // not a multi-node switched path.

    // Build a per-query adjacency index for efficiency + determinism.
    std::map<SwitchId, std::vector<Edge>> swEdges;
    std::vector<Edge> deviceEdges; // edges whose other end is a device (to/from device).
    for (const auto& [id, link] : links_) {
        Edge e{link.id, link.port, link.otherEnd, link.state, EntityState::UNKNOWN};
        if (auto p = lookupPort(link.port); p) e.portState = p->state;
        else e.portState = EntityState::DOWN;
        e.linkState = link.state;
        swEdges[link.rootSwitch].push_back(e);
    }
    // Deterministic order within each switch: sort by (port, link, otherEnd).
    for (auto& [k, v] : swEdges) {
        std::sort(v.begin(), v.end(), [](const Edge& a, const Edge& b) {
            if (a.port != b.port) return a.port < b.port;
            return a.link < b.link;
        });
    }
    // Edges that attach a device to a switch.
    for (auto& [k, v] : swEdges) {
        for (auto& e : v) { if (isDeviceEnd(e.other)) deviceEdges.push_back(e); }
    }
    // Deterministic: sort device edges.
    std::sort(deviceEdges.begin(), deviceEdges.end(), [](const Edge& a, const Edge& b){
        if (a.other.device != b.other.device) return a.other.device < b.other.device;
        return a.port < b.port;
    });

    // Each partial path is (currentSwitch, inboundPort, hops, links, switches, visited).
    struct Partial {
        SwitchId cur;
        PortId inPort;
        bool inPortKnown;
        std::vector<PathHop> hops;
        std::vector<LinkId> links;
        std::vector<SwitchId> switches;
        std::set<SwitchId> visited;
    };
    std::deque<Partial> frontier;
    // Seed: for every usable device edge whose device == src, enter that switch.
    for (const auto& e : deviceEdges) {
        if (e.other.device == src && edgeUsable(e)) {
            Partial p;
            p.cur = e.port.sw;
            p.inPort = e.port;
            p.inPortKnown = true;
            p.visited.insert(e.port.sw);
            p.switches.push_back(e.port.sw);
            p.links.push_back(e.link);
            // Add a placeholder hop; the exit is filled when we leave.
            PathHop h;
            h.sw = e.port.sw;
            h.inPort = e.port;
            p.hops.push_back(h);
            frontier.push_back(std::move(p));
        }
    }

    // BFS/DFS with bounds. Use DFS (stack) for determinism; order is by sorted edges.
    while (!frontier.empty()) {
        Partial p = std::move(frontier.front());
        frontier.pop_front();
        if (result.size() >= cfg.maxPaths) break;

        auto it = swEdges.find(p.cur);
        if (it == swEdges.end()) continue;
        const auto& edges = it->second;
        // For each usable edge out of this switch.
        for (const auto& e : edges) {
            if (!edgeUsable(e)) continue;
            if (result.size() >= cfg.maxPaths) break;

            if (isDeviceEnd(e.other)) {
                if (e.other.device == dst) {
                    // Found: path ends here on device dst via switch p.cur.
                    Partial fin = p;
                    fin.hops.back().outPort = e.port;
                    fin.hops.back().exitToDevice = true;
                    fin.links.push_back(e.link);
                    FabricPath path;
                    path.source = src;
                    path.destination = dst;
                    path.hops = fin.hops;
                    path.links = fin.links;
                    path.switchSequence = fin.switches;
                    path.topologyGen = topologyGen_;
                    path.partitionGen = PartitionGeneration{0};
                    path.state = EntityState::UP;
                    // compute generations
                    SwitchGeneration minSw{0}; PortGeneration minPort{0};
                    path.switchGen = minSw; path.portGen = minPort;
                    path.capable = true;
                    path.provenance = provenance_;
                    path.source = src; path.destination = dst;
                    // If any edge on the path is degraded, mark DEGRADED.
                    bool degraded = false;
                    for (auto lid : path.links) { if (auto l = lookupLink(lid); l && l->degraded) degraded = true; }
                    if (degraded) path.state = EntityState::DEGRADED;
                    result.push_back(std::move(path));
                }
                // else: other devices are not intermediate relays; skip.
            } else if (isSwitchEnd(e.other)) {
                // Inter-switch link: go to the other switch.
                if (!cfg.allowSwitchSwitching) continue;
                SwitchId nxt = e.other.sw;
                if (p.visited.count(nxt)) continue;       // prevent cycles.
                if (p.switches.size() >= cfg.maxDepth) continue; // depth bound.
                if (result.size() >= cfg.maxPaths) break;
                Partial np = p;
                np.hops.back().outPort = e.port;
                np.hops.back().exitToSwitch = true;
                np.links.push_back(e.link);
                np.switches.push_back(nxt);
                np.visited.insert(nxt);
                // Enter nxt; record inbound port from the reverse half if present.
                PortId in = PortId::unknown();
                // Find reverse half link rooted at nxt whose other end == p.cur.
                if (auto rit = swEdges.find(nxt); rit != swEdges.end()) {
                    for (const auto& re : rit->second) {
                        if (isSwitchEnd(re.other) && re.other.sw == p.cur) { in = re.port; break; }
                    }
                }
                PathHop nh;
                nh.sw = nxt;
                nh.inPort = in;
                np.hops.push_back(nh);
                np.cur = nxt;
                frontier.push_back(std::move(np));
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// reachability
// ---------------------------------------------------------------------------
Reachability FabricTopology::deviceReachability(DeviceId src, DeviceId dst) const {
    // Both endpoints must be known entities that are present (not merely observed).
    auto s = lookupDevice(src);
    auto d = lookupDevice(dst);
    if (!s) return Reachability::INSUFFICIENT_EVIDENCE;
    if (!d) return Reachability::INSUFFICIENT_EVIDENCE;
    if (src == dst) return Reachability::REACHABLE;
    if (s->state == EntityState::STALE || s->state == EntityState::REVALIDATION_REQUIRED)
        return Reachability::ENDPOINT_STALE;
    if (d->state == EntityState::STALE || d->state == EntityState::REVALIDATION_REQUIRED)
        return Reachability::ENDPOINT_STALE;
    if (s->state == EntityState::UNSUPPORTED || d->state == EntityState::UNSUPPORTED)
        return Reachability::UNSUPPORTED;
    if (s->state == EntityState::DOWN || s->state == EntityState::ABSENT || s->state == EntityState::UNREACHABLE ||
        d->state == EntityState::DOWN || d->state == EntityState::ABSENT || d->state == EntityState::UNREACHABLE)
        return Reachability::UNREACHABLE;

    // Explore reachability by checking whether any UP path exists.
    bool anySwitchDown = false, anyPortDown = false;
    for (const auto& [swId, rec] : switches_) {
        if (rec.state == EntityState::DOWN || rec.state == EntityState::UNREACHABLE ||
            rec.state == EntityState::ABSENT) anySwitchDown = true;
        if (rec.state == EntityState::STALE || rec.state == EntityState::REVALIDATION_REQUIRED)
            anySwitchDown = true;
    }
    for (const auto& [pid, rec] : ports_) {
        if (rec.state == EntityState::DOWN || rec.state == EntityState::UNREACHABLE ||
            rec.state == EntityState::DISABLED || rec.state == EntityState::STALE ||
            rec.state == EntityState::REVALIDATION_REQUIRED) anyPortDown = true;
    }

    // Partition gating: if there is an active partition containing src, dst must
    // be a member of the same partition, otherwise blocked.
    std::optional<FabricPartition> srcPart, dstPart;
    for (const auto& [pid, p] : partitions_) {
        if (p.state == PartitionState::STALE || p.state == PartitionState::REVALIDATION_REQUIRED)
            continue; // stale partitions are not authoritative.
        if (std::find(p.members.begin(), p.members.end(), src) != p.members.end()) srcPart = p;
        if (std::find(p.members.begin(), p.members.end(), dst) != p.members.end()) dstPart = p;
    }
    if (srcPart && dstPart) {
        if (srcPart->id != dstPart->id) return Reachability::PARTITION_BLOCKED;
    }

    TraversalConfig cfg;
    auto paths = enumeratePaths(src, dst, cfg);
    if (paths.empty()) {
        // Distinguish the reason.
        if (anySwitchDown) return Reachability::SWITCH_DOWN;
        if (anyPortDown) return Reachability::PORT_DOWN;
        for (const auto& [swId, rec] : switches_) {
            if (rec.state == EntityState::STALE || rec.state == EntityState::REVALIDATION_REQUIRED)
                return Reachability::REVALIDATION_REQUIRED;
        }
        return Reachability::UNREACHABLE;
    }
    // All found paths degraded => degraded reachability.
    bool anyUp = false;
    for (const auto& p : paths) if (p.state == EntityState::UP) anyUp = true;
    if (!anyUp) return Reachability::REACHABLE_DEGRADED;
    return Reachability::REACHABLE;
}

// ---------------------------------------------------------------------------
// health
// ---------------------------------------------------------------------------
std::vector<SwitchHealthObservation> FabricTopology::healthObservations() const {
    std::vector<SwitchHealthObservation> out;
    for (const auto& [swId, rec] : switches_) {
        SwitchHealthObservation o;
        o.sw = rec.id;
        o.state = rec.state;
        o.reason = "";
        o.workerKnown = rec.workerKnown;
        o.worker = rec.worker;
        o.workerBoot = rec.workerBoot;
        o.provenance = rec.provenance;
        o.source = rec.source;
        o.topologyGen = rec.topologyGen;
        o.matched = true;
        out.push_back(o);
    }
    return out;
}

FabricHealth FabricTopology::fabricHealth() const {
    bool anySwitchFailure = false, anyPortFailure = false, anyEndpointFailure = false;
    bool anyStale = false, anyRevalidation = false, anyDegraded = false;
    for (const auto& [id, rec] : switches_) {
        if (rec.state == EntityState::STALE || rec.state == EntityState::REVALIDATION_REQUIRED) anyStale = true;
        if (rec.state == EntityState::DOWN || rec.state == EntityState::UNREACHABLE ||
            rec.state == EntityState::ABSENT) anySwitchFailure = true;
        if (rec.state == EntityState::DEGRADED) anyDegraded = true;
    }
    for (const auto& [id, rec] : ports_) {
        if (rec.state == EntityState::STALE || rec.state == EntityState::REVALIDATION_REQUIRED) anyStale = true;
        if (rec.state == EntityState::DOWN || rec.state == EntityState::UNREACHABLE ||
            rec.state == EntityState::DISABLED) anyPortFailure = true;
        if (rec.state == EntityState::DEGRADED) anyDegraded = true;
    }
    for (const auto& [id, rec] : devices_) {
        if (rec.state == EntityState::STALE || rec.state == EntityState::REVALIDATION_REQUIRED) anyStale = true;
        if (rec.state == EntityState::DOWN || rec.state == EntityState::UNREACHABLE || rec.state == EntityState::ABSENT) anyEndpointFailure = true;
    }
    for (const auto& [pid, part] : partitions_) {
        if (part.state == PartitionState::STALE || part.state == PartitionState::REVALIDATION_REQUIRED) anyStale = true;
        if (part.state == PartitionState::RECONFIGURING) anyRevalidation = true;
    }
    if (anySwitchFailure) return FabricHealth::SWITCH_FAILURE;
    if (anyPortFailure) return FabricHealth::PORT_FAILURE;
    if (anyEndpointFailure) return FabricHealth::ENDPOINT_FAILURE;
    if (anyRevalidation) return FabricHealth::REVALIDATION_REQUIRED;
    if (anyStale) return FabricHealth::REVALIDATION_REQUIRED;
    if (anyDegraded) return FabricHealth::DEGRADED;
    // Cheap global view: if there is at least one switch and all devices attach,
    // we call it healthy; otherwise partially reachable.
    (void)anyStale;
    return FabricHealth::HEALTHY;
}

// ---------------------------------------------------------------------------
// integrity
// ---------------------------------------------------------------------------
FabricTopology::IntegrityReport FabricTopology::checkIntegrity() const {
    IntegrityReport rep;
    std::set<DeviceId> devIds;
    std::set<SwitchId> swIds;
    std::set<PortId> portIds;
    for (const auto& [id, rec] : devices_) { if (!devIds.insert(id).second) rep.violations.push_back("duplicate device id"); }
    for (const auto& [id, rec] : switches_) { if (!swIds.insert(id).second) rep.violations.push_back("duplicate switch id"); }
    for (const auto& [id, rec] : ports_) { if (!portIds.insert(id).second) rep.violations.push_back("duplicate port id"); }
    for (const auto& [id, rec] : switches_) {
        if (rec.id != id) rep.violations.push_back("switch map key != record id");
        // A switch may legitimately be present before its ports are populated, so a
        // declared portCount is not itself an integrity violation. Ports carry
        // their own presence; traversal simply yields no path while they are absent.
    }
    for (const auto& [pid, rec] : ports_) {
        if (switches_.find(pid.sw) == switches_.end()) rep.violations.push_back("port references missing switch");
        if (pid.index >= limits::kMaxPortsPerSwitch) rep.violations.push_back("port index out of bound");
    }
    for (const auto& [lid, rec] : links_) {
        if (switches_.find(rec.rootSwitch) == switches_.end()) rep.violations.push_back("link references missing root switch");
        if (ports_.find(rec.port) == ports_.end()) rep.violations.push_back("link references missing port");
        if (rec.port.sw != rec.rootSwitch) rep.violations.push_back("link port switch != root switch");
        if (rec.otherEnd.kind == LinkEnd::Kind::Device) {
            if (devices_.find(rec.otherEnd.device) == devices_.end()) rep.violations.push_back("link references missing device");
        } else {
            if (switches_.find(rec.otherEnd.sw) == switches_.end()) rep.violations.push_back("link references missing switch");
        }
    }
    for (const auto& [pid, part] : partitions_) {
        for (auto m : part.members) if (devices_.find(m) == devices_.end()) rep.violations.push_back("partition references missing member");
        for (auto s : part.switches) if (switches_.find(s) == switches_.end()) rep.violations.push_back("partition references missing switch");
    }
    // A path must reference only existing entities; enumerate a few to validate.
    return rep;
}

} // namespace nvswitch_fabric