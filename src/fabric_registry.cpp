// NVSwitch Fabric - FabricRegistry implementation.
#include "nvswitch_fabric/fabric_registry.h"
#include "nvswitch_fabric/persistence.h"
#include <algorithm>
#include <chrono>
#include <unordered_map>

namespace nvswitch_fabric {

namespace {
uint64_t nsNow() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}
bool isCurrentState(EntityState s) {
    return s == EntityState::UP || s == EntityState::DEGRADED || s == EntityState::PRESENT;
}
} // namespace

FabricRegistry::FabricRegistry() {
    auto t = std::make_shared<FabricTopology>();
    t->id_ = 1;
    t->label_ = "initial";
    nextSnapshotId_ = 2;
    current_ = t;   // convert to shared_ptr<const FabricTopology>
}

FabricRegistry::Snapshot FabricRegistry::snapshot() const {
    std::shared_lock<std::shared_mutex> lk(mutex_);
    return current_;
}

uint64_t FabricRegistry::nextSnapshotId() { std::unique_lock lk(mutex_); return nextSnapshotId_; }
uint64_t FabricRegistry::nextDecisionId() { std::unique_lock lk(mutex_); return nextDecisionId_; }
uint64_t FabricRegistry::nextObservationId() { std::unique_lock lk(mutex_); return nextObservationId_; }
uint64_t FabricRegistry::nextMeasurementId() { std::unique_lock lk(mutex_); return nextMeasurementId_; }

void FabricRegistry::finalizeProvenance(FabricTopology& cand) {
    bool anySyn = false, anyReal = false;
    auto upd = [&](Provenance pq){ if (pq == Provenance::SYNTHETIC) anySyn = true; else if (pq == Provenance::REAL) anyReal = true; };
    for (const auto& [k, x] : cand.devices_) upd(x.provenance);
    for (const auto& [k, x] : cand.switches_) upd(x.provenance);
    for (const auto& [k, x] : cand.ports_) upd(x.provenance);
    for (const auto& [k, x] : cand.links_) upd(x.provenance);
    for (const auto& [k, x] : cand.partitions_) upd(x.provenance);
    if (anySyn) cand.provenance_ = Provenance::SYNTHETIC;
    else if (anyReal) cand.provenance_ = Provenance::REAL;
    else cand.provenance_ = Provenance::UNSUPPORTED;
}
bool FabricRegistry::validateCandidate(FabricTopology& cand, std::string& why) const {
    finalizeProvenance(cand);
    auto report = cand.checkIntegrity();
    if (!report.ok()) { why = report.violations.front(); return false; }
    if (cand.deviceCount() > limits::kMaxDevices) { why = "too many devices"; return false; }
    if (cand.switchCount() > limits::kMaxSwitches) { why = "too many switches"; return false; }
    if (cand.portCount() > limits::kMaxPorts) { why = "too many ports"; return false; }
    if (cand.linkCount() > limits::kMaxLinks) { why = "too many links"; return false; }
    if (cand.partitionCount() > limits::kMaxPartitions) { why = "too many partitions"; return false; }
    // Generation monotonicity against the current snapshot.
    auto old = current_;
    for (auto& [id, rec] : cand.switches_) {
        if (auto o = old->lookupSwitch(id); o && rec.gen.get() < o->gen.get()) { why = "switch generation decreased"; return false; }
    }
    for (auto& [id, rec] : cand.ports_) {
        if (auto o = old->lookupPort(id); o && rec.gen.get() < o->gen.get()) { why = "port generation decreased"; return false; }
    }
    for (auto& [id, rec] : cand.links_) {
        if (auto o = old->lookupLink(id); o && rec.gen.get() < o->gen.get()) { why = "link generation decreased"; return false; }
    }
    for (auto& [id, rec] : cand.partitions_) {
        if (auto o = old->lookupPartition(id); o && rec.gen.get() < o->gen.get()) { why = "partition generation decreased"; return false; }
    }
    if (cand.topologyGen_.get() < old->topologyGen_.get()) { why = "topology generation decreased"; return false; }
    return true;
}
// Mutations below each take the exclusive lock, deep-copy the snapshot,
// mutate, validate, then atomically publish (copy-on-write).

Result<FabricRegistry::Snapshot> FabricRegistry::addDevice(const DeviceRecord& rec) {
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    if (cand.devices_.count(rec.id)) return Err(err(ErrorCode::ALREADY_EXISTS, "device exists"));
    if (cand.devices_.size() >= limits::kMaxDevices) return Err(err(ErrorCode::RESOURCE_LIMIT, "max devices"));
    cand.devices_[rec.id] = rec;
    cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::removeDevice(DeviceId id) {
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    if (!cand.devices_.erase(id)) return Err(err(ErrorCode::NOT_FOUND, "device not found"));
    // Remove links referencing this device.
    for (auto it = cand.links_.begin(); it != cand.links_.end();) {
        if (it->second.otherEnd.kind == LinkEnd::Kind::Device && it->second.otherEnd.device == id) it = cand.links_.erase(it);
        else ++it;
    }
    cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::addSwitch(const SwitchRecord& rec) {
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    if (cand.switches_.count(rec.id)) return Err(err(ErrorCode::ALREADY_EXISTS, "switch exists"));
    if (cand.switches_.size() >= limits::kMaxSwitches) return Err(err(ErrorCode::RESOURCE_LIMIT, "max switches"));
    cand.switches_[rec.id] = rec;
    cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::removeSwitch(SwitchId id) {
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    if (!cand.switches_.erase(id)) return Err(err(ErrorCode::NOT_FOUND, "switch not found"));
    for (auto it = cand.ports_.begin(); it != cand.ports_.end();) { if (it->first.sw == id) it = cand.ports_.erase(it); else ++it; }
    for (auto it = cand.links_.begin(); it != cand.links_.end();) { if (it->second.rootSwitch == id || (it->second.otherEnd.kind==LinkEnd::Kind::Switch && it->second.otherEnd.sw==id)) it = cand.links_.erase(it); else ++it; }
    cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::setSwitchState(SwitchId id, EntityState state, std::string reason) {
    (void)reason;
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    auto it = cand.switches_.find(id);
    if (it == cand.switches_.end()) return Err(err(ErrorCode::NOT_FOUND, "switch not found"));
    // Health changes that reduce capacity invalidate routes: advance switch gen
    // only when entering a failure/stale state so dependent routes lose
    // authority immediately.
    if (state == EntityState::DOWN || state == EntityState::UNREACHABLE ||
        state == EntityState::STALE || state == EntityState::REVALIDATION_REQUIRED ||
        state == EntityState::ABSENT) {
        it->second.gen = it->second.gen.next();
    }
    it->second.state = state;
    cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::restartSwitch(SwitchId id, SwitchGeneration newGen) {
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    auto it = cand.switches_.find(id);
    if (it == cand.switches_.end()) return Err(err(ErrorCode::NOT_FOUND, "switch not found"));
    if (newGen.get() < it->second.gen.get()) return Err(err(ErrorCode::STALE_GENERATION, "new gen < current"));
    it->second.gen = newGen;
    it->second.state = EntityState::REVALIDATION_REQUIRED;
    // Invalidate dynamic state owned by workers on this switch.
    cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::addPort(const SwitchPortRecord& rec) {
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    if (cand.switches_.find(rec.id.sw) == cand.switches_.end()) return Err(err(ErrorCode::INVALID_ARGUMENT, "port switch missing"));
    if (cand.ports_.count(rec.id)) return Err(err(ErrorCode::ALREADY_EXISTS, "port exists"));
    if (cand.ports_.size() >= limits::kMaxPorts) return Err(err(ErrorCode::RESOURCE_LIMIT, "max ports"));
    if (rec.id.index >= limits::kMaxPortsPerSwitch) return Err(err(ErrorCode::INVALID_ARGUMENT, "port index bound"));
    // Synchronize switch portCount.
    auto& sw = cand.switches_[rec.id.sw];
    if (rec.id.index >= sw.portCount) sw.portCount = rec.id.index + 1;
    cand.ports_[rec.id] = rec;
    cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::removePort(PortId id) {
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    if (!cand.ports_.erase(id)) return Err(err(ErrorCode::NOT_FOUND, "port not found"));
    for (auto it = cand.links_.begin(); it != cand.links_.end();) { if (it->second.port == id) it = cand.links_.erase(it); else ++it; }
    cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::setPortState(PortId id, EntityState state, std::string reason) {
    (void)reason;
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    auto it = cand.ports_.find(id);
    if (it == cand.ports_.end()) return Err(err(ErrorCode::NOT_FOUND, "port not found"));
    if (state == EntityState::DOWN || state == EntityState::UNREACHABLE ||
        state == EntityState::STALE || state == EntityState::REVALIDATION_REQUIRED ||
        state == EntityState::DISABLED || state == EntityState::ABSENT) {
        it->second.gen = it->second.gen.next();
    }
    it->second.state = state;
    cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::addLink(const LinkRecord& rec) {
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    if (cand.switches_.find(rec.rootSwitch) == cand.switches_.end()) return Err(err(ErrorCode::INVALID_ARGUMENT, "link root switch missing"));
    if (cand.ports_.find(rec.port) == cand.ports_.end()) return Err(err(ErrorCode::INVALID_ARGUMENT, "link port missing"));
    if (rec.port.sw != rec.rootSwitch) return Err(err(ErrorCode::INVALID_ARGUMENT, "link port sw != root"));
    if (rec.otherEnd.kind == LinkEnd::Kind::Device && cand.devices_.find(rec.otherEnd.device) == cand.devices_.end()) return Err(err(ErrorCode::INVALID_ARGUMENT, "link device missing"));
    if (rec.otherEnd.kind == LinkEnd::Kind::Switch && cand.switches_.find(rec.otherEnd.sw) == cand.switches_.end()) return Err(err(ErrorCode::INVALID_ARGUMENT, "link switch missing"));
    if (cand.links_.count(rec.id)) return Err(err(ErrorCode::ALREADY_EXISTS, "link exists"));
    if (cand.links_.size() >= limits::kMaxLinks) return Err(err(ErrorCode::RESOURCE_LIMIT, "max links"));
    cand.links_[rec.id] = rec;
    cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::removeLink(LinkId id) {
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    if (!cand.links_.erase(id)) return Err(err(ErrorCode::NOT_FOUND, "link not found"));
    cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::setLinkState(LinkId id, EntityState state, bool degraded, std::string reason) {
    (void)reason;
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    auto it = cand.links_.find(id);
    if (it == cand.links_.end()) return Err(err(ErrorCode::NOT_FOUND, "link not found"));
    if (state == EntityState::DOWN || state == EntityState::STALE || state == EntityState::REVALIDATION_REQUIRED || state == EntityState::UNREACHABLE) {
        it->second.gen = it->second.gen.next();
    }
    it->second.state = state;
    it->second.degraded = degraded;
    cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::publishPartition(const FabricPartition& part) {
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    auto it = cand.partitions_.find(part.id);
    if (it == cand.partitions_.end()) {
        if (cand.partitions_.size() >= limits::kMaxPartitions) return Err(err(ErrorCode::RESOURCE_LIMIT, "max partitions"));
        FabricPartition p = part;
        if (p.gen.get() == 0) p.gen = PartitionGeneration{1};
        cand.partitions_[p.id] = p;
    } else {
        // Generation-bound: a changed membership/state must advance the gen.
        FabricPartition existing = it->second;
        std::vector<DeviceId> oldM = existing.members, newM = part.members;
        std::sort(oldM.begin(), oldM.end()); std::sort(newM.begin(), newM.end());
        bool changed = (oldM != newM) || (existing.fabric != part.fabric) || (existing.state != part.state);
        FabricPartition p = part;
        if (changed) {
            p.gen = PartitionGeneration{ std::max(existing.gen.get(), p.gen.get()) + 1 };
        } else {
            p.gen = PartitionGeneration{ std::max(existing.gen.get(), p.gen.get()) };
        }
        cand.partitions_[p.id] = p;
    }
    // Validate referenced members are known devices.
    auto p = cand.partitions_[part.id];
    for (auto m : p.members) if (cand.devices_.find(m) == cand.devices_.end()) return Err(err(ErrorCode::INVALID_PARTITION, "partition member device missing"));
    for (auto sw : p.switches) if (cand.switches_.find(sw) == cand.switches_.end()) return Err(err(ErrorCode::INVALID_PARTITION, "partition member switch missing"));
    cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::removePartition(PartitionId id) {
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    if (!cand.partitions_.erase(id)) return Err(err(ErrorCode::NOT_FOUND, "partition not found"));
    cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::markPartitionStale(PartitionId id) {
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    auto it = cand.partitions_.find(id);
    if (it == cand.partitions_.end()) return Err(err(ErrorCode::NOT_FOUND, "partition not found"));
    if (it->second.state != PartitionState::STALE) it->second.gen = it->second.gen.next();
    it->second.state = PartitionState::STALE;
    cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::publishMeasurement(const Measurement& m) {
    std::unique_lock lk(mutex_);
    if (measurements_.size() >= limits::kMaxMeasurements) measurements_.pop_front();
    Measurement mm = m; mm.matched = true;
    if (mm.id.get() == 0) { mm.id = MeasurementId{nextMeasurementId_++}; }
    measurements_.push_back(std::move(mm));
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::publishCongestion(const CongestionObservation& c) {
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    // Merge congestion link degradation into the topology.
    for (const auto& ld : c.links) {
        if (!ld.linkKnown) continue;
        auto it = cand.links_.find(ld.link);
        if (it == cand.links_.end()) continue;
        it->second.degraded = ld.degraded;
        if (ld.degraded) it->second.state = EntityState::DEGRADED;
    }
    cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<void> FabricRegistry::recordDecision(const RouteDecision& d) {
    std::unique_lock lk(mutex_);
    decisionLog_.push_back(d);
    if (decisionLog_.size() > limits::kMaxHistory) decisionLog_.pop_front();
    return Ok();
}
Result<FabricRegistry::Snapshot> FabricRegistry::invalidateForWorker(WorkerId worker, WorkerBootId boot) {
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    bool any = false;
    for (auto& [id, rec] : cand.switches_) if (rec.workerKnown && rec.worker==worker && rec.workerBoot==boot) { rec.state = EntityState::REVALIDATION_REQUIRED; rec.gen = rec.gen.next(); any = true; }
    for (auto& [id, rec] : cand.ports_) if (rec.workerKnown && rec.worker==worker && rec.workerBoot==boot) { rec.state = EntityState::REVALIDATION_REQUIRED; rec.gen = rec.gen.next(); any = true; }
    for (auto& [id, rec] : cand.links_) if (rec.workerKnown && rec.worker==worker && rec.workerBoot==boot) { rec.state = EntityState::REVALIDATION_REQUIRED; rec.gen = rec.gen.next(); any = true; }
    for (auto& [id, rec] : cand.partitions_) if (rec.workerKnown && rec.worker==worker && rec.workerBoot==boot) { rec.state = PartitionState::REVALIDATION_REQUIRED; rec.gen = rec.gen.next(); any = true; }
    for (auto& [id, rec] : cand.devices_) if (rec.workerKnown && rec.worker==worker && rec.workerBoot==boot) { rec.state = EntityState::REVALIDATION_REQUIRED; any = true; }
    if (any) cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::expireDynamicEvidence() {
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    for (auto& [id, rec] : cand.switches_) if (rec.workerKnown || rec.provenance == Provenance::REAL) { if (rec.state != EntityState::REVALIDATION_REQUIRED) rec.state = EntityState::REVALIDATION_REQUIRED; }
    for (auto& [id, rec] : cand.ports_) if (rec.workerKnown || rec.provenance == Provenance::REAL) { if (rec.state != EntityState::REVALIDATION_REQUIRED) rec.state = EntityState::REVALIDATION_REQUIRED; }
    for (auto& [id, rec] : cand.links_) if (rec.workerKnown || rec.provenance == Provenance::REAL) { if (rec.state != EntityState::REVALIDATION_REQUIRED) rec.state = EntityState::REVALIDATION_REQUIRED; }
    for (auto& [id, rec] : cand.partitions_) if (rec.workerKnown || rec.provenance == Provenance::REAL) { if (rec.state != PartitionState::REVALIDATION_REQUIRED) rec.state = PartitionState::REVALIDATION_REQUIRED; }
    for (auto& [id, rec] : cand.devices_) if (rec.workerKnown || rec.provenance == Provenance::REAL) { if (rec.state != EntityState::REVALIDATION_REQUIRED) rec.state = EntityState::REVALIDATION_REQUIRED; }
    for (auto& m : measurements_) m.matched = false;
    cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::revalidateAll() {
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    for (auto& [id, rec] : cand.switches_) if (rec.state == EntityState::REVALIDATION_REQUIRED || rec.state == EntityState::STALE) { rec.state = EntityState::UP; }
    for (auto& [id, rec] : cand.ports_) if (rec.state == EntityState::REVALIDATION_REQUIRED || rec.state == EntityState::STALE) { rec.state = EntityState::UP; }
    for (auto& [id, rec] : cand.links_) if (rec.state == EntityState::REVALIDATION_REQUIRED || rec.state == EntityState::STALE) { rec.state = EntityState::UP; }
    for (auto& [id, rec] : cand.partitions_) if (rec.state == PartitionState::REVALIDATION_REQUIRED || rec.state == PartitionState::STALE) { rec.state = PartitionState::ACTIVE; }
    for (auto& [id, rec] : cand.devices_) if (rec.state == EntityState::REVALIDATION_REQUIRED || rec.state == EntityState::STALE) { rec.state = EntityState::UP; }
    cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::applyBulk(const BulkEdit& e) {
    std::unique_lock lk(mutex_);
    FabricTopology cand = *current_;
    // Stage all then validate references before commit.
    for (const auto& d : e.devices) cand.devices_[d.id] = d;
    for (const auto& s : e.switches) cand.switches_[s.id] = s;
    for (const auto& p : e.ports) cand.ports_[p.id] = p;
    for (const auto& l : e.links) cand.links_[l.id] = l;
    for (const auto& p : e.partitions) cand.partitions_[p.id] = p;
    cand.topologyGen_ = cand.topologyGen_.next();
    std::string why;
    if (!validateCandidate(cand, why)) return Err(err(ErrorCode::INVALID_TOPOLOGY, "bulk rejected: " + why));
    cand.id_ = nextSnapshotId_++; cand.epoch_=epoch_;
    current_ = std::make_shared<FabricTopology>(std::move(cand)); version_++; lastSnapshotNanos_=nsNow();
    return Ok(current_);
}
Result<FabricRegistry::Snapshot> FabricRegistry::applyBackendScan(const Backend::Scan& scan) {
    BulkEdit e;
    e.devices = scan.devices; e.switches = scan.switches; e.ports = scan.ports;
    e.links = scan.links; e.partitions = scan.partitions;
    return applyBulk(e);
}
Reachability FabricRegistry::reachability(DeviceId src, DeviceId dst) const {
    auto snap = snapshot();
    return snap->deviceReachability(src, dst);
}
FabricHealth FabricRegistry::health() const {
    auto snap = snapshot();
    return snap->fabricHealth();
}
std::vector<FabricPath> FabricRegistry::paths(DeviceId src, DeviceId dst, const TraversalConfig& cfg) const {
    auto snap = snapshot();
    return snap->enumeratePaths(src, dst, cfg);
}
Result<RouteDecision> FabricRegistry::route(DeviceId src, DeviceId dst, const RoutePolicy& policy) {
    Snapshot snap; std::vector<Measurement> measCopy;
    { std::shared_lock lk(mutex_); snap = current_; measCopy.assign(measurements_.begin(), measurements_.end()); }
    uint64_t did, obs;
    { std::unique_lock lk(mutex_); did = nextDecisionId_++; obs = nextObservationId_++; }
    RouteDecisionId rid{did}; ObservationId oid{obs};
    TopologySnapshotId sid{ "snap" + std::to_string(snap->idValue()) };
    auto r = computeRoute(*snap, src, dst, policy, &measCopy, rid, RouteGeneration{0}, sid, nsNow(), epoch_);
    if (r.ok()) {
        r.value().observation = oid; r.value().observationKnown = true;
        { std::unique_lock lk(mutex_); decisionLog_.push_back(r.value()); if (decisionLog_.size() > limits::kMaxHistory) decisionLog_.pop_front(); }
    }
    return r;
}
bool FabricRegistry::decisionStillAuthoritative(const RouteDecision& d) const {
    const auto snap = snapshot();
    return routeDecisionStillAuthoritative(d, *snap);
}
std::vector<RouteDecision> FabricRegistry::recentDecisions() const {
    Snapshot snap; std::deque<RouteDecision> copy;
    { std::shared_lock lk(mutex_); snap = current_; copy = decisionLog_; }
    std::vector<RouteDecision> out;
    for (auto& d : copy) { d.authoritative = routeDecisionStillAuthoritative(d, *snap) && d.authoritative; out.push_back(std::move(d)); }
    return out;
}
std::vector<Measurement> FabricRegistry::recentMeasurements() const {
    std::deque<Measurement> copy; { std::shared_lock lk(mutex_); copy = measurements_; }
    return std::vector<Measurement>(copy.begin(), copy.end());
}
Result<void> FabricRegistry::bindBackend(std::shared_ptr<Backend> b) {
    if (!b) return Err(err(ErrorCode::INVALID_ARGUMENT, "null backend"));
    std::unique_lock lk(mutex_); backend_ = std::move(b); return Ok();
}
Result<BackendCapabilities> FabricRegistry::backendCapabilities() const {
    std::shared_ptr<Backend> b; { std::shared_lock lk(mutex_); b = backend_; }
    if (!b) return Err(err(ErrorCode::BACKEND_UNAVAILABLE, "no backend bound"));
    return Ok(b->capabilities());
}
Result<void> FabricRegistry::scanBackend() {
    std::shared_ptr<Backend> b; { std::shared_lock lk(mutex_); b = backend_; }
    if (!b) return Err(err(ErrorCode::BACKEND_UNAVAILABLE, "no backend bound"));
    auto cap = b->capabilities();
    if (cap.provenance == Provenance::UNSUPPORTED) return Ok(); // nothing to scan.
    auto sc = b->scan();
    if (!sc.ok()) return Err(sc.error());
    auto res = applyBackendScan(sc.value());
    if (!res.ok()) return Err(res.error());
    return Ok();
}
Result<void> FabricRegistry::persistTo(const std::string& path) const {
    return Persistence::serializeToFile(*this, path);
}
Result<std::shared_ptr<FabricRegistry>> FabricRegistry::restoreFrom(const std::string& path) {
    return Persistence::deserializeFromFile(path);
}

} // namespace nvswitch_fabric
