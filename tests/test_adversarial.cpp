// NVSwitch Fabric - adversarial hardening tests.
#include "test_common.h"
#include "nvswitch_fabric/fabric_registry.h"
#include "nvswitch_fabric/backends/synthetic_backend.h"
#include "nvswitch_fabric/wire.h"
#include "nvswitch_fabric/runtime/transport.h"
#include "nvswitch_fabric/persistence.h"
#include <cmath>
#include <limits>

using namespace nvswitch_fabric;

TEST_CASE(dup_switch_rejected) {
    FabricRegistry reg;
    SwitchRecord s = makeSyntheticSwitch(SwitchId{10}, "S", 2);
    CHECK(reg.addSwitch(s).ok());
    CHECK(!reg.addSwitch(s).ok());   // ALREADY_EXISTS
    // Idempotent duplicate publication must not double-count entities.
    FabricRegistry::BulkEdit e; e.switches.push_back(s); e.switches.push_back(s);
    CHECK(reg.applyBulk(e).ok());
    CHECK(reg.snapshot()->switchCount() == 1);
}

TEST_CASE(impossible_port_index_rejected) {
    FabricRegistry reg; applyScenarioToRegistry(reg, ScenarioType::SINGLE_SWITCH_TWO_GPU);
    SwitchPortRecord p = makeSyntheticPort(SwitchId{10}, limits::kMaxPortsPerSwitch, LinkEnd::toDevice(DeviceId{5}));
    CHECK(!reg.addPort(p).ok());   // port index bound
}

TEST_CASE(invalid_references_rejected) {
    FabricRegistry reg; applyScenarioToRegistry(reg, ScenarioType::SINGLE_SWITCH_TWO_GPU);
    // Link referencing missing device / port / switch.
    FabricRegistry::BulkEdit e;
    e.links.push_back(makeSyntheticLink(LinkId{900}, SwitchId{10}, PortId{SwitchId{10}, 0}, LinkEnd::toDevice(DeviceId{888})));
    CHECK(!reg.applyBulk(e).ok());
    // Port referencing missing switch.
    FabricRegistry::BulkEdit e2; e2.ports.push_back(makeSyntheticPort(SwitchId{999}, 0, LinkEnd::toDevice(DeviceId{1})));
    CHECK(!reg.applyBulk(e2).ok());
    // Partition referencing missing member device.
    FabricRegistry::BulkEdit e3; e3.partitions.push_back(makeSyntheticPartition(PartitionId{"bad"}, {DeviceId{1}, DeviceId{777}}, {SwitchId{10}}, PartitionState::ACTIVE));
    CHECK(!reg.applyBulk(e3).ok());
}

TEST_CASE(nan_negative_values_safe) {
    Measurement m; m.elapsedNanos = 0; m.iterations = 0;
    double bw = m.bytesPerSec();
    CHECK(bw == 0.0);
    CHECK(!std::isnan(m.latencyUsPerOp()));
}

TEST_CASE(wire_string_absurd_rejected) {
    ByteWriter w; w.u32(0xFFFFFFFFu); // absurd length
    ByteReader r(w.data().data(), w.size());
    std::string s;
    CHECK(!r.str(s));
}

TEST_CASE(stale_route_authority_rejected) {
    FabricRegistry reg; applyScenarioToRegistry(reg, ScenarioType::TWO_SWITCH_REDUNDANT);
    RoutePolicy pol;
    auto d = reg.route(DeviceId{1}, DeviceId{2}, pol);
    CHECK(reg.decisionStillAuthoritative(d.value()));
    CHECK(reg.restartSwitch(SwitchId{10}, SwitchGeneration{99}).ok());
    CHECK(!reg.decisionStillAuthoritative(d.value()));
}

TEST_CASE(persistence_absurd_counts_rejected) {
    // A payload whose declared device count exceeds the hard bound must be rejected.
    ByteWriter p;
    p.u64(99); p.u64(1); p.u64(1); p.u64(0); p.u8((uint8_t)Provenance::SYNTHETIC); p.str("x"); p.u8(0);
    p.u32(limits::kMaxDevices + 10);   // absurd count
    auto res = Persistence::decodeTopology(p.data());
    CHECK(!res.ok());
}

TEST_CASE(frame_malformed_rejected) {
    using namespace nvswitch_fabric::rt;
    // Good frame roundtrip.
    Frame f; f.type = MsgType::PING; f.payload = {1,2,3,4,5};
    auto enc = encodeFrame(f);
    auto dec = decodeFrame(enc);
    CHECK(dec.ok()); CHECK(dec.value().type == MsgType::PING); CHECK(dec.value().payload.size() == 5);
    // Corrupt magic.
    auto bad = enc; bad[0] ^= 0xFF;
    CHECK(!decodeFrame(bad).ok());
    // Corrupt payload CRC.
    auto bad2 = enc; bad2[enc.size()-1] ^= 0xFF;
    CHECK(!decodeFrame(bad2).ok());
    // Truncated.
    std::vector<uint8_t> tr(enc.begin(), enc.begin() + 10);
    CHECK(!decodeFrame(tr).ok());
    // Absurd length in header.
    FrameStream fs;
    std::vector<uint8_t> hdr(16); hdr[0]=0x31; hdr[1]=0x46; hdr[2]=0x56; hdr[3]=0x4E; hdr[4]=1; hdr[5]=2;
    hdr[8]=0xFF; hdr[9]=0xFF; hdr[10]=0xFF; hdr[11]=0x7F;  // huge length
    std::vector<Frame> out;
    CHECK(!fs.feed(hdr.data(), hdr.size(), out));
    // A complete frame with garbage magic must be rejected by the stream.
    FrameStream fs2; std::vector<Frame> out2;
    std::vector<uint8_t> badMagic = enc; badMagic[0] = 0x00; badMagic[1] = 0x00; badMagic[2] = 0x00; badMagic[3] = 0x00;
    CHECK(!fs2.feed(badMagic.data(), badMagic.size(), out2));
}

NVF_MAIN