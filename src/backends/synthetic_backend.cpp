// NVSwitch Fabric - synthetic backend implementation.
#include "nvswitch_fabric/backends/synthetic_backend.h"
#include <algorithm>

namespace nvswitch_fabric {

namespace {
constexpr Provenance kSyn = Provenance::SYNTHETIC;
constexpr EvidenceSource kSynSrc = EvidenceSource::SYNTHETIC_FIXTURE;
}

DeviceRecord makeSyntheticDevice(DeviceId id, const std::string& gen, EntityState st) {
    DeviceRecord d;
    d.id = id;
    d.generation.name = gen; d.generation.observed = true;
    d.present = (st == EntityState::UP || st == EntityState::PRESENT || st == EntityState::DEGRADED);
    d.backendId = "synthetic-device-" + std::to_string(id.get());
    d.vendor = "Synthetic";
    d.product = "SynthGPU";
    d.state = st;
    d.provenance = kSyn; d.source = kSynSrc;
    return d;
}
SwitchRecord makeSyntheticSwitch(SwitchId id, const std::string& generation, uint32_t portCount) {
    SwitchRecord s;
    s.id = id;
    s.gen = SwitchGeneration{1};
    s.generationName = generation; s.generationObserved = true;
    s.vendor = "Synthetic";
    s.product = "SynthSwitch";
    s.family = generation;
    s.portCount = portCount;
    s.state = EntityState::UP;
    s.provenance = kSyn; s.source = kSynSrc;
    return s;
}
SwitchPortRecord makeSyntheticPort(SwitchId sw, uint32_t index, const LinkEnd& attached) {
    SwitchPortRecord p;
    p.id = PortId{sw, index};
    p.gen = PortGeneration{1};
    p.attached = attached; p.attachedKnown = true;
    p.state = EntityState::UP;
    p.activeLinkCount = 1; p.activeLinkCountKnown = true;
    p.nominalBandwidthMBps = 900 * 1024;   // 900 GB/s nominal (synthetic).
    p.nominalBandwidthKnown = true;
    p.provenance = kSyn; p.source = kSynSrc;
    return p;
}
LinkRecord makeSyntheticLink(LinkId id, SwitchId root, PortId port, const LinkEnd& other) {
    LinkRecord l;
    l.id = id;
    l.gen = LinkGeneration{1};
    l.rootSwitch = root;
    l.port = port;
    l.otherEnd = other; l.otherEndKnown = true;
    l.state = EntityState::UP;
    l.provenance = kSyn; l.source = kSynSrc;
    return l;
}
FabricPartition makeSyntheticPartition(PartitionId id, std::vector<DeviceId> members,
                                       std::vector<SwitchId> switches, PartitionState st) {
    FabricPartition p;
    p.id = id; p.gen = PartitionGeneration{1};
    p.fabric = FabricId{"synthetic-fabric"};
    std::sort(members.begin(), members.end()); members.erase(std::unique(members.begin(), members.end()), members.end());
    p.members = members;
    std::sort(switches.begin(), switches.end()); switches.erase(std::unique(switches.begin(), switches.end()), switches.end());
    p.switches = switches;
    p.state = st;
    p.provenance = kSyn; p.source = kSynSrc;
    return p;
}

Backend::Scan buildSyntheticScan(ScenarioType type) {
    Backend::Scan s;
    auto attach = [&](){};

    auto addDeviceLink = [&](DeviceId d, SwitchId sw, uint32_t portIdx, uint64_t linkId) {
        LinkEnd end = LinkEnd::toDevice(d);
        s.devices.push_back(makeSyntheticDevice(d, "Synthetic", EntityState::UP));
        s.ports.push_back(makeSyntheticPort(sw, portIdx, end));
        s.links.push_back(makeSyntheticLink(LinkId{linkId}, sw, PortId{sw, portIdx}, end));
    };

    switch (type) {
        case ScenarioType::EMPTY: break;

        case ScenarioType::SINGLE_SWITCH_TWO_GPU: {
            SwitchId sw{10};
            s.switches.push_back(makeSyntheticSwitch(sw, "SynthSwitch-1", 2));
            addDeviceLink(DeviceId{1}, sw, 0, 100);
            addDeviceLink(DeviceId{2}, sw, 1, 101);
            break;
        }
        case ScenarioType::SINGLE_SWITCH_MANY_GPU: {
            SwitchId sw{10};
            s.switches.push_back(makeSyntheticSwitch(sw, "SynthSwitch-1", 4));
            for (uint32_t i = 0; i < 4; ++i) addDeviceLink(DeviceId{i+1}, sw, i, 100 + i);
            break;
        }
        case ScenarioType::TWO_SWITCH_REDUNDANT: {
            // Two devices, two switches, every device on every switch (redundant paths).
            SwitchId swA{10}, swB{11};
            s.switches.push_back(makeSyntheticSwitch(swA, "SynthSwitch-1", 3));
            s.switches.push_back(makeSyntheticSwitch(swB, "SynthSwitch-1", 3));
            // Device 0 on swA port0, swB port0.
            addDeviceLink(DeviceId{1}, swA, 0, 100);
            addDeviceLink(DeviceId{1}, swB, 0, 101);
            addDeviceLink(DeviceId{2}, swA, 1, 102);
            addDeviceLink(DeviceId{2}, swB, 1, 103);
            // Inter-switch links (both halves).
            s.ports.push_back(makeSyntheticPort(swA, 2, LinkEnd::toSwitch(swB)));
            s.ports.push_back(makeSyntheticPort(swB, 2, LinkEnd::toSwitch(swA)));
            s.links.push_back(makeSyntheticLink(LinkId{120}, swA, PortId{swA,2}, LinkEnd::toSwitch(swB)));
            s.links.push_back(makeSyntheticLink(LinkId{121}, swB, PortId{swB,2}, LinkEnd::toSwitch(swA)));
            break;
        }
        case ScenarioType::TWO_SWITCH_LINEAR: {
            SwitchId swA{10}, swB{11};
            s.switches.push_back(makeSyntheticSwitch(swA, "SynthSwitch-1", 2));
            s.switches.push_back(makeSyntheticSwitch(swB, "SynthSwitch-1", 2));
            addDeviceLink(DeviceId{1}, swA, 0, 100);
            addDeviceLink(DeviceId{2}, swB, 0, 101);
            s.ports.push_back(makeSyntheticPort(swA, 1, LinkEnd::toSwitch(swB)));
            s.ports.push_back(makeSyntheticPort(swB, 1, LinkEnd::toSwitch(swA)));
            s.links.push_back(makeSyntheticLink(LinkId{120}, swA, PortId{swA,1}, LinkEnd::toSwitch(swB)));
            s.links.push_back(makeSyntheticLink(LinkId{121}, swB, PortId{swB,1}, LinkEnd::toSwitch(swA)));
            break;
        }
        case ScenarioType::DENSE_MULTI_SWITCH: {
            // 4 devices, 3 switches forming a moderately dense mesh.
            SwitchId swA{10}, swB{11}, swC{12};
            s.switches.push_back(makeSyntheticSwitch(swA, "SynthSwitch-1", 4));
            s.switches.push_back(makeSyntheticSwitch(swB, "SynthSwitch-1", 4));
            s.switches.push_back(makeSyntheticSwitch(swC, "SynthSwitch-1", 4));
            for (uint32_t i = 0; i < 4; ++i) {
                addDeviceLink(DeviceId{i+1}, swA, i, 200 + i);
                addDeviceLink(DeviceId{i+1}, swB, i, 210 + i);
                addDeviceLink(DeviceId{i+1}, swC, i, 220 + i);
            }
            // Inter-switch full mesh (both halves).
            auto mesh = [&](SwitchId x, SwitchId y, uint32_t px, uint32_t py, uint64_t lid) {
                s.ports.push_back(makeSyntheticPort(x, px, LinkEnd::toSwitch(y)));
                s.ports.push_back(makeSyntheticPort(y, py, LinkEnd::toSwitch(x)));
                s.links.push_back(makeSyntheticLink(LinkId{lid}, x, PortId{x,px}, LinkEnd::toSwitch(y)));
                s.links.push_back(makeSyntheticLink(LinkId{lid+1}, y, PortId{y,py}, LinkEnd::toSwitch(x)));
            };
            mesh(swA, swB, 0, 0, 300);
            mesh(swA, swC, 1, 0, 320);
            mesh(swB, swC, 1, 1, 340);
            break;
        }
        case ScenarioType::PARTITION_SPLIT: {
            SwitchId sw{10};
            s.switches.push_back(makeSyntheticSwitch(sw, "SynthSwitch-1", 4));
            for (uint32_t i = 0; i < 4; ++i) addDeviceLink(DeviceId{i+1}, sw, i, 100 + i);
            s.partitions.push_back(makeSyntheticPartition(PartitionId{"p0"}, {DeviceId{1}, DeviceId{2}}, {sw}, PartitionState::ACTIVE));
            s.partitions.push_back(makeSyntheticPartition(PartitionId{"p1"}, {DeviceId{3}, DeviceId{4}}, {sw}, PartitionState::ACTIVE));
            break;
        }
        case ScenarioType::DEGRADED_PORT: {
            SwitchId sw{10};
            s.switches.push_back(makeSyntheticSwitch(sw, "SynthSwitch-1", 2));
            addDeviceLink(DeviceId{1}, sw, 0, 100);
            addDeviceLink(DeviceId{2}, sw, 1, 101);
            // Degrade port 1.
            for (auto& p : s.ports) if (p.id == PortId{sw,1}) p.state = EntityState::DEGRADED;
            for (auto& l : s.links) if (l.port == PortId{sw,1}) { l.state = EntityState::DEGRADED; l.degraded = true; }
            break;
        }
        case ScenarioType::PARALLEL_LINKS: {
            // Two devices connected to one switch via parallel links (two links between
            // device0 and switch port pair).
            SwitchId sw{10};
            s.switches.push_back(makeSyntheticSwitch(sw, "SynthSwitch-1", 3));
            addDeviceLink(DeviceId{1}, sw, 0, 100);
            addDeviceLink(DeviceId{2}, sw, 1, 101);
            addDeviceLink(DeviceId{2}, sw, 2, 102);   // second parallel link for device1.
            break;
        }
    }
    for (auto& d : s.devices) if (d.state != EntityState::UP) d.present = true;
    return s;
}

SyntheticBackend::SyntheticBackend(ScenarioType type) : type_(type) {}
Result<void> SyntheticBackend::initialize() {
    scan_ = buildSyntheticScan(type_);
    return Ok();
}
void SyntheticBackend::shutdown() noexcept {}
BackendCapabilities SyntheticBackend::capabilities() const {
    BackendCapabilities c;
    c.name = "synthetic";
    c.deviceDiscovery = true; c.switchDiscovery = true; c.portDiscovery = true;
    c.linkDiscovery = true; c.partitionDiscovery = true;
    c.switchManagement = true; c.telemetry = true; c.congestionCounters = true;
    c.nvSwitchPresent = false;
    c.provenance = Provenance::SYNTHETIC;
    return c;
}
Result<Backend::Scan> SyntheticBackend::scan() { return Ok(scan_); }

Result<FabricRegistry::Snapshot> applyScenarioToRegistry(FabricRegistry& reg, ScenarioType type) {
    FabricRegistry::BulkEdit e;
    auto s = buildSyntheticScan(type);
    e.devices = std::move(s.devices); e.switches = std::move(s.switches);
    e.ports = std::move(s.ports); e.links = std::move(s.links); e.partitions = std::move(s.partitions);
    return reg.applyBulk(e);
}

} // namespace nvswitch_fabric