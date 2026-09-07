// NVSwitch Fabric - persistence implementation.
#include "nvswitch_fabric/persistence.h"
#include "nvswitch_fabric/topology.h"
#include "nvswitch_fabric/records.h"
#include <cstring>
#include <fstream>

namespace nvswitch_fabric {

namespace {
constexpr uint8_t kMagic[8] = {'N','V','S','F','F','A','B','1'};
constexpr uint32_t kFormatVersion = 1;

inline Error integrityErr() {
    return Error{ErrorCode::INTEGRITY_FAILURE, "corrupt persistence"};
}
template <typename T> Result<T> integrity() {
    return Result<T>(integrityErr());
}
inline Error unsupportedFuture() {
    return Error{ErrorCode::UNSUPPORTED, "unsupported future persistence version"};
}

void encodeDevice(ByteWriter& w, const DeviceRecord& r) {
    w.u64(r.id.get()); w.str(r.generation.name); w.u8(r.generation.observed?1:0);
    w.u8(r.present?1:0); w.str(r.backendId); w.str(r.vendor); w.str(r.product);
    w.u8((uint8_t)r.state); w.str(r.partition.value); w.u64(r.attachedSwitch.get());
    w.u8(r.attachedSwitchKnown?1:0);
    w.str(r.worker.value); w.u8(r.workerKnown?1:0); w.u64(r.workerBoot);
    w.u8((uint8_t)r.provenance); w.u8((uint8_t)r.source);
    w.u64(r.topologyGen.get());
}
bool decodeDevice(ByteReader& r, DeviceRecord& rec) {
    uint64_t id; if (!r.u64(id)) return false; rec.id = DeviceId{id};
    if (!r.str(rec.generation.name)) return false;
    uint8_t go; if (!r.u8(go)) return false; rec.generation.observed = go!=0;
    uint8_t present; if (!r.u8(present)) return false; rec.present = present!=0;
    if (!r.str(rec.backendId)) return false; if (!r.str(rec.vendor)) return false; if (!r.str(rec.product)) return false;
    uint8_t st; if (!r.u8(st)) return false; rec.state = (EntityState)st;
    if (!r.str(rec.partition.value)) return false;
    uint64_t sw; if (!r.u64(sw)) return false; rec.attachedSwitch = SwitchId{sw};
    uint8_t ask; if (!r.u8(ask)) return false; rec.attachedSwitchKnown = ask!=0;
    if (!r.str(rec.worker.value)) return false; uint8_t wk; if (!r.u8(wk)) return false; rec.workerKnown = wk!=0;
    uint64_t wb; if (!r.u64(wb)) return false; rec.workerBoot = wb;
    uint8_t p; if (!r.u8(p)) return false; rec.provenance = (Provenance)p;
    uint8_t s; if (!r.u8(s)) return false; rec.source = (EvidenceSource)s;
    uint64_t tg; if (!r.u64(tg)) return false; rec.topologyGen = TopologyGeneration{tg};
    return true;
}
void encodeSwitch(ByteWriter& w, const SwitchRecord& r) {
    w.u64(r.id.get()); w.u64(r.gen.get()); w.str(r.generationName);
    w.u8(r.generationObserved?1:0); w.str(r.vendor); w.str(r.product); w.str(r.family);
    w.u32(r.portCount); w.u8((uint8_t)r.state); w.str(r.fabric.value); w.u8(r.fabricKnown?1:0);
    w.str(r.partition.value); w.u8(r.partitionKnown?1:0); w.str(r.worker.value); w.u8(r.workerKnown?1:0);
    w.u64(r.workerBoot); w.u8((uint8_t)r.provenance); w.u8((uint8_t)r.source); w.u64(r.topologyGen.get());
}
bool decodeSwitch(ByteReader& r, SwitchRecord& rec) {
    uint64_t id; if (!r.u64(id)) return false; rec.id = SwitchId{id};
    uint64_t g; if (!r.u64(g)) return false; rec.gen = SwitchGeneration{g};
    if (!r.str(rec.generationName)) return false;
    uint8_t go; if (!r.u8(go)) return false; rec.generationObserved = go!=0;
    if (!r.str(rec.vendor)) return false; if (!r.str(rec.product)) return false; if (!r.str(rec.family)) return false;
    uint32_t pc; if (!r.u32(pc)) return false; rec.portCount = pc;
    uint8_t st; if (!r.u8(st)) return false; rec.state = (EntityState)st;
    if (!r.str(rec.fabric.value)) return false; uint8_t fk; if (!r.u8(fk)) return false; rec.fabricKnown = fk!=0;
    if (!r.str(rec.partition.value)) return false; uint8_t pk; if (!r.u8(pk)) return false; rec.partitionKnown = pk!=0;
    if (!r.str(rec.worker.value)) return false; uint8_t wk; if (!r.u8(wk)) return false; rec.workerKnown = wk!=0;
    uint64_t wb; if (!r.u64(wb)) return false; rec.workerBoot = wb;
    uint8_t p; if (!r.u8(p)) return false; rec.provenance = (Provenance)p;
    uint8_t s; if (!r.u8(s)) return false; rec.source = (EvidenceSource)s;
    uint64_t tg; if (!r.u64(tg)) return false; rec.topologyGen = TopologyGeneration{tg};
    return true;
}
void encodePort(ByteWriter& w, const SwitchPortRecord& r) {
    w.u64(r.id.sw.get()); w.u32(r.id.index); w.u64(r.gen.get());
    w.u8((uint8_t)r.attached.kind); w.u64(r.attached.device.get()); w.u64(r.attached.sw.get());
    w.u8(r.attachedKnown?1:0); w.u8((uint8_t)r.state); w.u32(r.activeLinkCount);
    w.u8(r.activeLinkCountKnown?1:0); w.u64(r.nominalBandwidthMBps); w.u8(r.nominalBandwidthKnown?1:0);
    w.str(r.worker.value); w.u8(r.workerKnown?1:0); w.u64(r.workerBoot);
    w.u8((uint8_t)r.provenance); w.u8((uint8_t)r.source); w.u64(r.topologyGen.get());
}
bool decodePort(ByteReader& r, SwitchPortRecord& rec) {
    uint64_t sw; if (!r.u64(sw)) return false; uint32_t idx; if (!r.u32(idx)) return false; rec.id = PortId{SwitchId{sw}, idx};
    uint64_t g; if (!r.u64(g)) return false; rec.gen = PortGeneration{g};
    uint8_t kind; if (!r.u8(kind)) return false; rec.attached.kind = (LinkEnd::Kind)kind;
    uint64_t dv; if (!r.u64(dv)) return false; rec.attached.device = DeviceId{dv};
    uint64_t sw2; if (!r.u64(sw2)) return false; rec.attached.sw = SwitchId{sw2};
    uint8_t ak; if (!r.u8(ak)) return false; rec.attachedKnown = ak!=0;
    uint8_t st; if (!r.u8(st)) return false; rec.state = (EntityState)st;
    uint32_t alc; if (!r.u32(alc)) return false; rec.activeLinkCount = alc; uint8_t alck; if (!r.u8(alck)) return false; rec.activeLinkCountKnown = alck!=0;
    uint64_t nb; if (!r.u64(nb)) return false; rec.nominalBandwidthMBps = nb; uint8_t nbk; if (!r.u8(nbk)) return false; rec.nominalBandwidthKnown = nbk!=0;
    if (!r.str(rec.worker.value)) return false; uint8_t wk; if (!r.u8(wk)) return false; rec.workerKnown = wk!=0;
    uint64_t wb; if (!r.u64(wb)) return false; rec.workerBoot = wb;
    uint8_t p; if (!r.u8(p)) return false; rec.provenance = (Provenance)p;
    uint8_t s; if (!r.u8(s)) return false; rec.source = (EvidenceSource)s;
    uint64_t tg; if (!r.u64(tg)) return false; rec.topologyGen = TopologyGeneration{tg};
    return true;
}
void encodeLink(ByteWriter& w, const LinkRecord& r) {
    w.u64(r.id.get()); w.u64(r.gen.get()); w.u64(r.rootSwitch.get());
    w.u64(r.port.sw.get()); w.u32(r.port.index);
    w.u8((uint8_t)r.otherEnd.kind); w.u64(r.otherEnd.device.get()); w.u64(r.otherEnd.sw.get());
    w.u8(r.otherEndKnown?1:0); w.u8((uint8_t)r.state); w.u8(r.degraded?1:0);
    w.str(r.worker.value); w.u8(r.workerKnown?1:0); w.u64(r.workerBoot);
    w.u8((uint8_t)r.provenance); w.u8((uint8_t)r.source); w.u64(r.topologyGen.get());
}
bool decodeLink(ByteReader& r, LinkRecord& rec) {
    uint64_t id; if (!r.u64(id)) return false; rec.id = LinkId{id};
    uint64_t g; if (!r.u64(g)) return false; rec.gen = LinkGeneration{g};
    uint64_t rs; if (!r.u64(rs)) return false; rec.rootSwitch = SwitchId{rs};
    uint64_t psw; if (!r.u64(psw)) return false; uint32_t pidx; if (!r.u32(pidx)) return false; rec.port = PortId{SwitchId{psw}, pidx};
    uint8_t kind; if (!r.u8(kind)) return false; rec.otherEnd.kind = (LinkEnd::Kind)kind;
    uint64_t dv; if (!r.u64(dv)) return false; rec.otherEnd.device = DeviceId{dv};
    uint64_t sw; if (!r.u64(sw)) return false; rec.otherEnd.sw = SwitchId{sw};
    uint8_t ok; if (!r.u8(ok)) return false; rec.otherEndKnown = ok!=0;
    uint8_t st; if (!r.u8(st)) return false; rec.state = (EntityState)st;
    uint8_t dg; if (!r.u8(dg)) return false; rec.degraded = dg!=0;
    if (!r.str(rec.worker.value)) return false; uint8_t wk; if (!r.u8(wk)) return false; rec.workerKnown = wk!=0;
    uint64_t wb; if (!r.u64(wb)) return false; rec.workerBoot = wb;
    uint8_t p; if (!r.u8(p)) return false; rec.provenance = (Provenance)p;
    uint8_t s; if (!r.u8(s)) return false; rec.source = (EvidenceSource)s;
    uint64_t tg; if (!r.u64(tg)) return false; rec.topologyGen = TopologyGeneration{tg};
    return true;
}
void encodePartition(ByteWriter& w, const FabricPartition& r) {
    w.str(r.id.value); w.u64(r.gen.get()); w.str(r.fabric.value);
    w.u32((uint32_t)r.members.size()); for (auto m : r.members) w.u64(m.get());
    w.u32((uint32_t)r.switches.size()); for (auto s : r.switches) w.u64(s.get());
    w.u8((uint8_t)r.state); w.str(r.worker.value); w.u8(r.workerKnown?1:0); w.u64(r.workerBoot);
    w.u8((uint8_t)r.provenance); w.u8((uint8_t)r.source); w.u64(r.topologyGen.get());
}
bool decodePartition(ByteReader& r, FabricPartition& rec) {
    if (!r.str(rec.id.value)) return false;
    uint64_t g; if (!r.u64(g)) return false; rec.gen = PartitionGeneration{g};
    if (!r.str(rec.fabric.value)) return false;
    uint32_t mc; if (!r.u32(mc)) return false; if (mc > limits::kMaxDevices) return false; rec.members.clear();
    for (uint32_t i=0;i<mc;++i){uint64_t m; if(!r.u64(m))return false; rec.members.push_back(DeviceId{m});}
    uint32_t sc; if (!r.u32(sc)) return false; if (sc > limits::kMaxSwitches) return false; rec.switches.clear();
    for (uint32_t i=0;i<sc;++i){uint64_t s; if(!r.u64(s))return false; rec.switches.push_back(SwitchId{s});}
    uint8_t st; if (!r.u8(st)) return false; rec.state = (PartitionState)st;
    if (!r.str(rec.worker.value)) return false; uint8_t wk; if (!r.u8(wk)) return false; rec.workerKnown = wk!=0;
    uint64_t wb; if (!r.u64(wb)) return false; rec.workerBoot = wb;
    uint8_t p; if (!r.u8(p)) return false; rec.provenance = (Provenance)p;
    uint8_t s; if (!r.u8(s)) return false; rec.source = (EvidenceSource)s;
    uint64_t tg; if (!r.u64(tg)) return false; rec.topologyGen = TopologyGeneration{tg};
    return true;
}

} // namespace (anon, codecs)

void Persistence::encodeTopologyPayload(ByteWriter& w, const FabricTopology& t) {
    w.u64(t.id_); w.u64(t.topologyGen_.get()); w.u64(t.fabricGen_.get()); w.u64(t.epoch_);
    w.u8((uint8_t)t.provenance_); w.str(t.label_); w.u8(t.persisted_?1:0);
    w.u32((uint32_t)t.devices_.size()); for (const auto& [id, r] : t.devices_) encodeDevice(w, r);
    w.u32((uint32_t)t.switches_.size()); for (const auto& [id, r] : t.switches_) encodeSwitch(w, r);
    w.u32((uint32_t)t.ports_.size()); for (const auto& [id, r] : t.ports_) encodePort(w, r);
    w.u32((uint32_t)t.links_.size()); for (const auto& [id, r] : t.links_) encodeLink(w, r);
    w.u32((uint32_t)t.partitions_.size()); for (const auto& [id, r] : t.partitions_) encodePartition(w, r);
}

Result<FabricTopology> Persistence::decodeTopologyPayload(ByteReader& r) {
    FabricTopology t;
    uint64_t id; if (!r.u64(id)) return integrity<FabricTopology>(); t.id_ = id;
    uint64_t tg; if (!r.u64(tg)) return integrity<FabricTopology>(); t.topologyGen_ = TopologyGeneration{tg};
    uint64_t fg; if (!r.u64(fg)) return integrity<FabricTopology>(); t.fabricGen_ = FabricGeneration{fg};
    uint64_t ep; if (!r.u64(ep)) return integrity<FabricTopology>(); t.epoch_ = ep;
    uint8_t pv; if (!r.u8(pv)) return integrity<FabricTopology>(); t.provenance_ = (Provenance)pv;
    if (!r.str(t.label_)) return integrity<FabricTopology>();
    uint8_t per; if (!r.u8(per)) return integrity<FabricTopology>(); t.persisted_ = per!=0;
    uint32_t dc; if (!r.u32(dc)) return integrity<FabricTopology>();
    if (dc > limits::kMaxDevices) return integrity<FabricTopology>();
    for (uint32_t i=0;i<dc;++i){DeviceRecord rec; if(!decodeDevice(r,rec))return integrity<FabricTopology>(); if(t.devices_.count(rec.id))return integrity<FabricTopology>(); t.devices_[rec.id]=rec;}
    uint32_t sc; if (!r.u32(sc)) return integrity<FabricTopology>();
    if (sc > limits::kMaxSwitches) return integrity<FabricTopology>();
    for (uint32_t i=0;i<sc;++i){SwitchRecord rec; if(!decodeSwitch(r,rec))return integrity<FabricTopology>(); if(t.switches_.count(rec.id))return integrity<FabricTopology>(); t.switches_[rec.id]=rec;}
    uint32_t pc; if (!r.u32(pc)) return integrity<FabricTopology>();
    if (pc > limits::kMaxPorts) return integrity<FabricTopology>();
    for (uint32_t i=0;i<pc;++i){SwitchPortRecord rec; if(!decodePort(r,rec))return integrity<FabricTopology>(); if(t.ports_.count(rec.id))return integrity<FabricTopology>(); t.ports_[rec.id]=rec;}
    uint32_t lc; if (!r.u32(lc)) return integrity<FabricTopology>();
    if (lc > limits::kMaxLinks) return integrity<FabricTopology>();
    for (uint32_t i=0;i<lc;++i){LinkRecord rec; if(!decodeLink(r,rec))return integrity<FabricTopology>(); if(t.links_.count(rec.id))return integrity<FabricTopology>(); t.links_[rec.id]=rec;}
    uint32_t par; if (!r.u32(par)) return integrity<FabricTopology>();
    if (par > limits::kMaxPartitions) return integrity<FabricTopology>();
    for (uint32_t i=0;i<par;++i){FabricPartition rec; if(!decodePartition(r,rec))return integrity<FabricTopology>(); if(t.partitions_.count(rec.id))return integrity<FabricTopology>(); t.partitions_[rec.id]=rec;}
    if (!r.ok()) return integrity<FabricTopology>();
    if (!r.eof()) return integrity<FabricTopology>();
    auto rep = t.checkIntegrity();
    if (!rep.ok()) return Result<FabricTopology>(Error{ErrorCode::INTEGRITY_FAILURE, rep.violations.front()});
    return Ok(std::move(t));
}

namespace { // anon (writeFileAtomic)
void writeFileAtomic(const std::string& path, const std::vector<uint8_t>& data) {
    std::string tmp = path + ".tmp";
    { std::ofstream f(tmp, std::ios::binary | std::ios::trunc); f.write((const char*)data.data(), (std::streamsize)data.size()); }
    std::remove(path.c_str());
    std::rename(tmp.c_str(), path.c_str());
}
} // namespace

Result<std::vector<uint8_t>> Persistence::serialize(const FabricRegistry& reg) {
    auto topo = reg.snapshot();
    ByteWriter w;
    w.raw(kMagic, 8);
    w.u32(kFormatVersion);
    uint32_t hdrCrc = crc32(w.data().data(), 12);
    w.u32(hdrCrc);
    ByteWriter payload; encodeTopologyPayload(payload, *topo);
    w.raw(payload.data().data(), payload.size());
    uint32_t payloadCrc = crc32(payload.data().data(), payload.size());
    w.u32(payloadCrc);
    return Ok(w.data());
}
Result<void> Persistence::serializeToFile(const FabricRegistry& reg, const std::string& path) {
    auto res = serialize(reg);
    if (!res.ok()) return Err(res.error());
    const auto& data = res.value();
    if (data.size() > limits::kMaxPersistenceBytes) return Err(err(ErrorCode::RESOURCE_LIMIT, "persistence too large"));
    writeFileAtomic(path, data);
    return Ok();
}
Result<std::shared_ptr<FabricRegistry>> Persistence::deserializeFromFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return Err(err(ErrorCode::NOT_FOUND, "persistence file not found"));
    std::streamsize sz = f.tellg();
    if (sz < 0) return integrity<std::shared_ptr<FabricRegistry>>();
    if ((uint64_t)sz > limits::kMaxPersistenceBytes) return Err(err(ErrorCode::RESOURCE_LIMIT, "persistence too large"));
    std::vector<uint8_t> data((size_t)sz);
    f.seekg(0); f.read((char*)data.data(), sz);
    return Persistence::deserialize(data);
}
Result<std::vector<uint8_t>> Persistence::encodeTopology(const FabricTopology& topo) {
    ByteWriter w; encodeTopologyPayload(w, topo); return Ok(w.data());
}
Result<FabricTopology> Persistence::decodeTopology(const uint8_t* p, size_t n) {
    ByteReader r(p, n); return decodeTopologyPayload(r);
}
Result<FabricTopology> Persistence::decodeTopology(const std::vector<uint8_t>& v) {
    return decodeTopology(v.data(), v.size());
}
Result<std::shared_ptr<FabricRegistry>> Persistence::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < 8 + 4 + 4 + 4) return integrity<std::shared_ptr<FabricRegistry>>();
    ByteReader r(data.data(), data.size());
    uint8_t mag[8]; if (!r.bytes(mag, 8)) return integrity<std::shared_ptr<FabricRegistry>>();
    if (std::memcmp(mag, kMagic, 8) != 0) return integrity<std::shared_ptr<FabricRegistry>>();
    uint32_t ver; if (!r.u32(ver)) return integrity<std::shared_ptr<FabricRegistry>>();
    if (ver != kFormatVersion) {
        if (ver > kFormatVersion) return Err(unsupportedFuture());
        return integrity<std::shared_ptr<FabricRegistry>>();
    }
    uint32_t hdrCrc; if (!r.u32(hdrCrc)) return integrity<std::shared_ptr<FabricRegistry>>();
    if (crc32(data.data(), 12) != hdrCrc) return integrity<std::shared_ptr<FabricRegistry>>();
    size_t headPos = r.position();
    size_t payloadLen = data.size() - headPos - 4;
    if (payloadLen == 0) return integrity<std::shared_ptr<FabricRegistry>>();
    const uint8_t* payload = data.data() + headPos;
    uint32_t payloadCrc = crc32(payload, payloadLen);
    uint32_t storedCrc = 0;
    for (int i = 0; i < 4; ++i) storedCrc |= ((uint32_t)data[headPos + payloadLen + i]) << (8 * i);
    if (storedCrc != payloadCrc) return integrity<std::shared_ptr<FabricRegistry>>();
    auto topo = decodeTopology(payload, payloadLen);
    if (!topo.ok()) return Err(topo.error());
    auto reg = std::make_shared<FabricRegistry>();
    reg->current_ = std::make_shared<FabricTopology>(std::move(topo.value()));
    reg->version_ = 1; reg->lastSnapshotNanos_ = 0;
    reg->nextSnapshotId_ = reg->current_->idValue() + 1;
    reg->epoch_ = reg->current_->epoch();
    return Ok(std::move(reg));
}

} // namespace nvswitch_fabric