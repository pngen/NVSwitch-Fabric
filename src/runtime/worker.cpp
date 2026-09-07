// NVSwitch Fabric - worker implementation.
#include "nvswitch_fabric/runtime/worker.h"
#include "nvswitch_fabric/version.h"
#include "nvswitch_fabric/persistence.h"
#include "nvswitch_fabric/wire.h"
#include "nvswitch_fabric/route.h"
#include "nvswitch_fabric/backends/synthetic_backend.h"
#include <cstring>
#include <chrono>
#include <thread>
#include <csignal>

namespace nvswitch_fabric {
namespace rt {

namespace {
void writeDouble(ByteWriter& w, double d) { uint64_t bits; std::memcpy(&bits, &d, 8); w.u64(bits); }
std::vector<uint8_t> encodePolicyLite(const RoutePolicy& p) {
    ByteWriter w; w.str(p.id.value); w.u8(p.requireSufficientsSwitches?1:0);
    w.u8(p.requireSufficientsPorts?1:0); w.u8(p.requireNonDegradedPath?1:0); w.u8(p.insidePartition?1:0);
    w.u64(p.maxAllowedSwitches); w.u64(p.maxAllowedHops); writeDouble(w, p.requiredMinRelativeBandwidth);
    w.u8(p.preferRedundantPath?1:0); w.u8(p.requireFreshEvidence?1:0); w.u64(p.maxStalenessNanos);
    w.u32((uint32_t)p.factors.size());
    for (const auto& [f, wt] : p.factors) { w.u8((uint8_t)f); writeDouble(w, wt); }
    return w.data();
}
} // namespace

Worker::Worker() {}
Worker::~Worker() { close(); }

Result<uint8_t> Worker::connect(const std::string& host, uint16_t port,
                                const WorkerId& worker, WorkerBootId boot, CoordinatorEpoch epoch) {
    auto cr = sock_.connectTo(host, port);
    if (!cr.ok()) return Err(cr.error());
    host_ = host; port_ = port;
    { ByteWriter h; h.u8(kWireProtocolVersion); auto fr = sendFrame(MsgType::HELLO, h.data()); if (!fr.ok()) return Err(fr.error()); }
    { ByteWriter w; w.u8(kWireProtocolVersion); w.u64(epoch); w.str(worker.value); w.u64(boot);
      auto fr = sendFrame(MsgType::REGISTER, w.data()); if (!fr.ok()) return Err(fr.error()); }
    worker_ = worker; boot_ = boot;
    return Ok(kWireProtocolVersion);
}

Result<void> Worker::publishTopology(const FabricTopology& topo, CoordinatorEpoch epoch) {
    auto blob = Persistence::encodeTopology(topo);
    if (!blob.ok()) return Err(blob.error());
    ByteWriter b; b.u8(kWireProtocolVersion); b.u64(epoch); b.str(worker_.value); b.u64(boot_);
    b.u32((uint32_t)blob.value().size()); b.raw(blob.value().data(), blob.value().size());
    return sendFrame(MsgType::PUBLISH_TOPOLOGY, b.data());
}
Result<void> Worker::publishMeasurement(const Measurement& m, CoordinatorEpoch epoch) {
    (void)epoch;
    ByteWriter b; b.u64(epoch);
    b.u64(m.source.get()); b.u64(m.destination.get()); b.u64(m.payloadBytes); b.u64(m.iterations); b.u64(m.elapsedNanos);
    return sendFrame(MsgType::PUBLISH_MEASUREMENT, b.data());
}
Result<void> Worker::sendFrame(MsgType type, const std::vector<uint8_t>& payload) {
    auto frame = encodeFrame(Frame{type, payload});
    return sock_.sendAll(frame.data(), frame.size());
}
Result<Frame> Worker::recvFrame() {
    std::vector<uint8_t> buf(4096);
    for (;;) {
        auto rr = sock_.recvSome(buf.data(), buf.size());
        if (!rr.ok()) return Err(rr.error());
        if (rr.value() == 0) return Err(err(ErrorCode::PROTOCOL_ERROR, "connection closed"));
        std::vector<Frame> frames;
        if (!stream_.feed(buf.data(), rr.value(), frames)) return Err(stream_.error());
        if (!frames.empty()) return Ok(std::move(frames.front()));
    }
}
Result<RouteDecision> Worker::queryRoute(DeviceId src, DeviceId dst, const RoutePolicy& policy, CoordinatorEpoch epoch) {
    (void)epoch;
    ByteWriter b; b.u64(src.get()); b.u64(dst.get());
    auto pp = encodePolicyLite(policy); b.u32((uint32_t)pp.size()); b.raw(pp.data(), pp.size());
    auto fr = sendFrame(MsgType::QUERY_ROUTE, b.data()); if (!fr.ok()) return Err(fr.error());
    auto rf = recvFrame(); if (!rf.ok()) return Err(rf.error());
    if (rf.value().type != MsgType::ROUTE_RESULT) return Err(err(ErrorCode::PROTOCOL_ERROR, "expected route result"));
    ByteReader r(rf.value().payload.data(), rf.value().payload.size());
    RouteDecision d;
    uint8_t o; if (!r.u8(o)) return Err(err(ErrorCode::PROTOCOL_ERROR, "outcome")); d.outcome = (RouteOutcome)o;
    if (!r.str(d.outcomeReason)) return Err(err(ErrorCode::PROTOCOL_ERROR, "reason"));
    std::string key; if (!r.str(key)) return Err(err(ErrorCode::PROTOCOL_ERROR, "key"));
    uint32_t cc; if (!r.u32(cc)) return Err(err(ErrorCode::PROTOCOL_ERROR, "candidates"));
    uint8_t au; if (!r.u8(au)) return Err(err(ErrorCode::PROTOCOL_ERROR, "authoritative")); d.authoritative = au!=0;
    (void)cc;
    d.source = src; d.destination = dst;
    if (!key.empty()) { d.selected = FabricPath{}; d.selected->source = src; d.selected->destination = dst; }
    return Ok(std::move(d));
}
void Worker::close() { sock_.close(); }

int workerMain(int argc, char** argv) {
    WsaGuard wsa;
    if (argc < 8) return 2;
    std::string host = argv[1];
    uint16_t port = (uint16_t)std::atoi(argv[2]);
    WorkerId worker{argv[3]};
    WorkerBootId boot = std::strtoull(argv[4], nullptr, 10);
    CoordinatorEpoch epoch = std::strtoull(argv[5], nullptr, 10);
    ScenarioType scenario = (ScenarioType)std::atoi(argv[6]);
    bool stay = (std::atoi(argv[7]) != 0);
    Worker w;
    auto c = w.connect(host, port, worker, boot, epoch);
    if (!c.ok()) return 3;
    // Build scenario topology and publish.
    FabricRegistry reg;
    applyScenarioToRegistry(reg, scenario);
    auto snap = reg.snapshot();
    auto pub = w.publishTopology(*snap, epoch);
    if (!pub.ok()) return 4;
    // Measure a couple of synthetic measurements.
    if (scenario != ScenarioType::EMPTY) {
        Measurement m; m.source = DeviceId{1}; m.destination = DeviceId{2};
        m.sourceKnown = m.destinationKnown = true; m.payloadBytes = 8ull*1024*1024; m.iterations = 10;
        m.elapsedNanos = 5000000; m.elapsedKnown = true; m.kind = MeasurementKind::SYNTHETIC_FIXTURE;
        m.provenance = Provenance::SYNTHETIC; m.integrityVerified = true;
        (void)w.publishMeasurement(m, epoch);
    }
    if (stay) for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return 0;
}

} // namespace rt
} // namespace nvswitch_fabric