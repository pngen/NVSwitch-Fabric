// NVSwitch Fabric - coordinator implementation.
#include "nvswitch_fabric/runtime/coordinator.h"
#include "nvswitch_fabric/version.h"
#include "nvswitch_fabric/persistence.h"
#include "nvswitch_fabric/wire.h"
#include "nvswitch_fabric/route.h"
#include "nvswitch_fabric/records.h"
#include <cstring>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstdlib>

namespace nvswitch_fabric {
namespace rt {

namespace {
uint64_t nsNow() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}
void writeDouble(ByteWriter& w, double d) { uint64_t bits; std::memcpy(&bits, &d, 8); w.u64(bits); }
bool readDouble(ByteReader& r, double& d) { uint64_t bits; if (!r.u64(bits)) return false; std::memcpy(&d, &bits, 8); return true; }

std::vector<uint8_t> encodePolicy(const RoutePolicy& p) {
    ByteWriter w; w.str(p.id.value); w.u8(p.requireSufficientsSwitches?1:0);
    w.u8(p.requireSufficientsPorts?1:0); w.u8(p.requireNonDegradedPath?1:0); w.u8(p.insidePartition?1:0);
    w.u64(p.maxAllowedSwitches); w.u64(p.maxAllowedHops); writeDouble(w, p.requiredMinRelativeBandwidth);
    w.u8(p.preferRedundantPath?1:0); w.u8(p.requireFreshEvidence?1:0); w.u64(p.maxStalenessNanos);
    w.u32((uint32_t)p.factors.size());
    for (const auto& [f, wt] : p.factors) { w.u8((uint8_t)f); writeDouble(w, wt); }
    return w.data();
}
Result<RoutePolicy> decodePolicy(const uint8_t* p, size_t n) {
    ByteReader r(p, n); RoutePolicy pol;
    if (!r.str(pol.id.value)) return Err(err(ErrorCode::PROTOCOL_ERROR, "policy id"));
    uint8_t a;
    if (!r.u8(a)) return Err(err(ErrorCode::PROTOCOL_ERROR, "policy field")); pol.requireSufficientsSwitches = a!=0;
    if (!r.u8(a)) return Err(err(ErrorCode::PROTOCOL_ERROR, "policy field")); pol.requireSufficientsPorts = a!=0;
    if (!r.u8(a)) return Err(err(ErrorCode::PROTOCOL_ERROR, "policy field")); pol.requireNonDegradedPath = a!=0;
    if (!r.u8(a)) return Err(err(ErrorCode::PROTOCOL_ERROR, "policy field")); pol.insidePartition = a!=0;
    if (!r.u64(pol.maxAllowedSwitches)) return Err(err(ErrorCode::PROTOCOL_ERROR, "policy field"));
    if (!r.u64(pol.maxAllowedHops)) return Err(err(ErrorCode::PROTOCOL_ERROR, "policy field"));
    if (!readDouble(r, pol.requiredMinRelativeBandwidth)) return Err(err(ErrorCode::PROTOCOL_ERROR, "policy field"));
    if (!r.u8(a)) return Err(err(ErrorCode::PROTOCOL_ERROR, "policy field")); pol.preferRedundantPath = a!=0;
    if (!r.u8(a)) return Err(err(ErrorCode::PROTOCOL_ERROR, "policy field")); pol.requireFreshEvidence = a!=0;
    if (!r.u64(pol.maxStalenessNanos)) return Err(err(ErrorCode::PROTOCOL_ERROR, "policy field"));
    uint32_t fc; if (!r.u32(fc)) return Err(err(ErrorCode::PROTOCOL_ERROR, "policy field"));
    if (fc > 24) return Err(err(ErrorCode::PROTOCOL_ERROR, "factor count"));
    pol.factors.clear();
    for (uint32_t i = 0; i < fc; ++i) {
        uint8_t f; if (!r.u8(f)) return Err(err(ErrorCode::PROTOCOL_ERROR, "factor"));
        double wt; if (!readDouble(r, wt)) return Err(err(ErrorCode::PROTOCOL_ERROR, "factor weight"));
        pol.factors.emplace_back((RouteFactor)f, wt);
    }
    return Ok(std::move(pol));
}
std::vector<uint8_t> encodeDecision(const RouteDecision& d) {
    ByteWriter w; w.u8((uint8_t)d.outcome); w.str(d.outcomeReason);
    w.str(d.selected ? d.selected->canonicalKey() : std::string{});
    w.u32((uint32_t)d.candidatePaths.size());
    w.u8(d.authoritative?1:0); w.u64(d.epoch); w.u64(d.topologyGen.get()); w.u64(d.partitionGen.get());
    return w.data();
}
Result<RouteDecision> decodeDecision(const uint8_t* p, size_t n) {
    ByteReader r(p, n); RouteDecision d;
    uint8_t o; if (!r.u8(o)) return Err(err(ErrorCode::PROTOCOL_ERROR, "outcome")); d.outcome = (RouteOutcome)o;
    if (!r.str(d.outcomeReason)) return Err(err(ErrorCode::PROTOCOL_ERROR, "reason"));
    std::string key; if (!r.str(key)) return Err(err(ErrorCode::PROTOCOL_ERROR, "key"));
    uint32_t cc; if (!r.u32(cc)) return Err(err(ErrorCode::PROTOCOL_ERROR, "candidates"));
    uint8_t au; if (!r.u8(au)) return Err(err(ErrorCode::PROTOCOL_ERROR, "authoritative")); d.authoritative = au!=0;
    if (!r.u64(d.epoch)) return Err(err(ErrorCode::PROTOCOL_ERROR, "epoch"));
    uint64_t tg; if (!r.u64(tg)) return Err(err(ErrorCode::PROTOCOL_ERROR, "topogen")); d.topologyGen = TopologyGeneration{tg};
    uint64_t pg; if (!r.u64(pg)) return Err(err(ErrorCode::PROTOCOL_ERROR, "partgen")); d.partitionGen = PartitionGeneration{pg};
    if (!key.empty()) { FabricPath fp; fp.source = d.source; fp.destination = d.destination; d.selected = fp; }
    return Ok(std::move(d));
}
} // namespace

Coordinator::Coordinator() {}
Coordinator::~Coordinator() { stop(); }

Result<void> Coordinator::start(uint16_t port) {
    auto lr = listen_.listenOn(port);
    if (!lr.ok()) return Err(lr.error());
    port_ = listen_.localPort();
    running_.store(true);
    acceptThread_ = std::thread([this]{ acceptLoop(); });
    return Ok();
}

void Coordinator::stop() {
    bool was = running_.exchange(false);
    if (was) {
        listen_.close();
        if (acceptThread_.joinable()) acceptThread_.join();
    }
    { std::lock_guard<std::mutex> lk(liveMutex_); for (auto& s : liveSocks_) if (s) s->close(); }
    std::vector<std::thread> ts;
    { std::lock_guard<std::mutex> lk(threadsMutex_); ts.swap(threads_); }
    for (auto& t : ts) if (t.joinable()) t.join();
}

void Coordinator::acceptLoop() {
    while (running_.load()) {
        auto acc = listen_.accept();
        if (!acc.ok()) break;
        if (liveConnections() >= limits::kMaxConnections) { acc.value().close(); continue; }
        auto sock = std::make_shared<TcpSocket>(std::move(acc.value()));
        uint64_t id = connSeq_.fetch_add(1);
        { std::lock_guard<std::mutex> lk(liveMutex_); liveSocks_.push_back(sock); }
        { std::lock_guard<std::mutex> lk(threadsMutex_); threads_.push_back(std::thread([this, sock, id]{ handleConnection(sock, id); })); }
    }
}

size_t Coordinator::liveConnections() const {
    std::lock_guard<std::mutex> lk(liveMutex_);
    return liveSocks_.size();
}

void Coordinator::applyWorkerTopology(const FabricTopology& topo, const WorkerId& worker, WorkerBootId boot) {
    FabricRegistry::BulkEdit e;
    for (const auto& [id, d] : topo.devices()) { DeviceRecord r = d; r.workerKnown = true; r.worker = worker; r.workerBoot = boot; e.devices.push_back(r); }
    for (const auto& [id, s] : topo.switches()) { SwitchRecord r = s; r.workerKnown = true; r.worker = worker; r.workerBoot = boot; e.switches.push_back(r); }
    for (const auto& [id, p] : topo.ports()) { SwitchPortRecord r = p; r.workerKnown = true; r.worker = worker; r.workerBoot = boot; e.ports.push_back(r); }
    for (const auto& [id, l] : topo.links()) { LinkRecord r = l; r.workerKnown = true; r.worker = worker; r.workerBoot = boot; e.links.push_back(r); }
    for (const auto& [id, p] : topo.partitions()) { FabricPartition r = p; r.workerKnown = true; r.worker = worker; r.workerBoot = boot; e.partitions.push_back(r); }
    (void)registry_.applyBulk(e);
}

void Coordinator::handleConnection(std::shared_ptr<TcpSocket> sock, uint64_t connId) {
    (void)connId;
    FrameStream stream;
    WorkerId worker; WorkerBootId boot = 0; bool registered = false;
    std::vector<uint8_t> buf(4096);
    for (;;) {
        auto rr = sock->recvSome(buf.data(), buf.size());
        if (!rr.ok() || rr.value() == 0) break;
        std::vector<Frame> frames;
        if (!stream.feed(buf.data(), rr.value(), frames)) break;
        for (auto& f : frames) {
            ByteReader r(f.payload.data(), f.payload.size());
            switch (f.type) {
                case MsgType::HELLO: { uint8_t ver; if (!r.u8(ver)) break; break; }
                case MsgType::REGISTER: {
                    uint8_t ver; if (!r.u8(ver)) break;
                    uint64_t ep; if (!r.u64(ep)) break;
                    std::string wid; if (!r.str(wid)) break;
                    uint64_t b; if (!r.u64(b)) break;
                    if (ep < registry_.epoch()) break;
                    WorkerId w{ wid };
                    {
                        std::lock_guard<std::mutex> lk(authMutex_);
                        auto mit = maxBoot_.find(w);
                        if (mit != maxBoot_.end() && b < mit->second) break;   // stale boot replay.
                        auto ait = activeBoot_.find(w);
                        if (ait != activeBoot_.end() && ait->second == b) break;   // duplicate active incarnation.
                        if (mit == maxBoot_.end() || b > mit->second) maxBoot_[w] = b;
                        activeBoot_[w] = b;
                    }
                    worker = w; boot = b; registered = true;
                    break;
                }
                case MsgType::PUBLISH_TOPOLOGY: {
                    if (!registered) break;
                    uint8_t ver; if (!r.u8(ver)) break;
                    uint64_t ep; if (!r.u64(ep)) break;
                    std::string wid; if (!r.str(wid)) break;
                    uint64_t b; if (!r.u64(b)) break;
                    uint32_t blobLen; if (!r.u32(blobLen)) break;
                    if (ep != registry_.epoch()) break;
                    if (WorkerId{wid} != worker || b != boot) break;
                    if (blobLen > limits::kMaxFrameBytes) break;
                    std::vector<uint8_t> blob(blobLen);
                    if (!r.bytes(blob.data(), blobLen)) break;
                    auto topo = Persistence::decodeTopology(blob);
                    if (!topo.ok()) break;
                    applyWorkerTopology(topo.value(), worker, boot);
                    if (!persistPath_.empty()) (void)registry_.persistTo(persistPath_);
                    break;
                }
                case MsgType::PUBLISH_MEASUREMENT: {
                    if (!registered) break;
                    uint64_t ep; if (!r.u64(ep)) break;
                    if (ep != registry_.epoch()) break;
                    uint64_t src, dst, bytes, iters, nanos;
                    if (!r.u64(src) || !r.u64(dst) || !r.u64(bytes) || !r.u64(iters) || !r.u64(nanos)) break;
                    Measurement m; m.source = DeviceId{src}; m.destination = DeviceId{dst};
                    m.sourceKnown = m.destinationKnown = true;
                    m.payloadBytes = bytes; m.iterations = iters; m.elapsedNanos = nanos; m.elapsedKnown = true;
                    m.kind = MeasurementKind::SYNTHETIC_FIXTURE; m.provenance = Provenance::SYNTHETIC;
                    m.workerKnown = true; m.worker = worker; m.workerBoot = boot; m.integrityVerified = true;
                    (void)registry_.publishMeasurement(m);
                    break;
                }
                case MsgType::QUERY_ROUTE: {
                    if (!registered) break;
                    uint64_t src, dst; if (!r.u64(src) || !r.u64(dst)) break;
                    uint32_t polLen; if (!r.u32(polLen)) break;
                    if (polLen > limits::kMaxFrameBytes) break;
                    std::vector<uint8_t> pol(polLen);
                    if (!r.bytes(pol.data(), polLen)) break;
                    auto polRes = decodePolicy(pol.data(), pol.size());
                    if (!polRes.ok()) break;
                    auto dec = registry_.route(DeviceId{src}, DeviceId{dst}, polRes.value());
                    if (!dec.ok()) break;
                    auto body = encodeDecision(dec.value());
                    auto frame = encodeFrame(Frame{MsgType::ROUTE_RESULT, body});
                    (void)sock->sendAll(frame.data(), frame.size());
                    break;
                }
                case MsgType::PING: {
                    auto pong = encodeFrame(Frame{MsgType::PONG, {}});
                    (void)sock->sendAll(pong.data(), pong.size());
                    break;
                }
                default: break;
            }
        }
    }
    // Connection closed => the worker (a real OS process) died or left.
    if (registered) { std::lock_guard<std::mutex> lk(authMutex_); auto ait = activeBoot_.find(worker); if (ait != activeBoot_.end() && ait->second == boot) activeBoot_.erase(ait); (void)registry_.invalidateForWorker(worker, boot); }
    { std::lock_guard<std::mutex> lk(liveMutex_);
      liveSocks_.erase(std::remove_if(liveSocks_.begin(), liveSocks_.end(), [&](const std::shared_ptr<TcpSocket>& s){ return s.get() == sock.get(); }), liveSocks_.end());
    }
    sock->close();
}

Result<void> Coordinator::persistTo(const std::string& path) const { return registry_.persistTo(path); }
Result<void> Coordinator::restoreFromAndRevalidate(const std::string& path) {
    auto res = FabricRegistry::restoreFrom(path);
    if (!res.ok()) return Err(res.error());
    auto snap = res.value()->snapshot();
    FabricRegistry::BulkEdit e;
    for (const auto& [id, d] : snap->devices()) e.devices.push_back(d);
    for (const auto& [id, s] : snap->switches()) e.switches.push_back(s);
    for (const auto& [id, p] : snap->ports()) e.ports.push_back(p);
    for (const auto& [id, l] : snap->links()) e.links.push_back(l);
    for (const auto& [id, p] : snap->partitions()) e.partitions.push_back(p);
    (void)registry_.applyBulk(e);
    (void)registry_.expireDynamicEvidence();
    return Ok();
}

int coordinatorMain(int argc, char** argv) {
    WsaGuard wsa;
    uint16_t port = 0;
    std::string persistPath = "nvswitch_fabric_state.nvf";
    CoordinatorEpoch epoch = 1;
    if (argc >= 2) port = (uint16_t)std::atoi(argv[1]);
    if (argc >= 3) persistPath = argv[2];
    if (argc >= 4) epoch = std::strtoull(argv[3], nullptr, 10);
    Coordinator coord;
    coord.setEpoch(epoch);
    coord.setPersistPath(persistPath);
    if (argc >= 3) { auto rr = coord.restoreFromAndRevalidate(persistPath); (void)rr; }
    auto st = coord.start(port);
    if (!st.ok()) return 2;
    for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return 0;
}

} // namespace rt
} // namespace nvswitch_fabric
