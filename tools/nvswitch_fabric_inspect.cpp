// NVSwitch Fabric - inspection CLI.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include "nvswitch_fabric/fabric_registry.h"
#include "nvswitch_fabric/backends/synthetic_backend.h"
#include "nvswitch_fabric/backends/cuda_measurement.h"
#include "nvswitch_fabric/persistence.h"

#include "nvswitch_fabric/backends/nvidia_backend.h"

using namespace nvswitch_fabric;

namespace {

// Deterministic mini-JSON: object keys are std::map (sorted), so output order is
// independent of insertion order.
struct Json {
    std::string s;
    void beginObj(){ s+="{"; }
    void endObj(){ s+="}"; }
    void beginArr(){ s+="["; }
    void endArr(){ s+="]"; }
    void sep(bool& first) { if (!first) s+=","; first=false; }
    void key(const std::string& k){ s += "\"" + k + "\":"; }
    void str(const std::string& v){ s += "\"" + v + "\""; }
    void num(double v){ char b[64]; std::snprintf(b, sizeof(b), "%g", v); s += b; }
    void u64j(uint64_t v){ s += std::to_string(v); }
    void boolj(bool v){ s += v ? "true" : "false"; }
};

struct Options {
    std::string scenario = "SINGLE_SWITCH_TWO_GPU";
    bool json = false;
    std::string persisted;
    bool nvidia = false;
    bool cuda = false;
    std::string action = "topology";
    DeviceId a{1}, b{2};
};

void buildRegistry(FabricRegistry& reg, const Options& opt) {
    ScenarioType st = ScenarioType::SINGLE_SWITCH_TWO_GPU;
    if (opt.scenario == "SINGLE_SWITCH_TWO_GPU") st = ScenarioType::SINGLE_SWITCH_TWO_GPU;
    else if (opt.scenario == "SINGLE_SWITCH_MANY_GPU") st = ScenarioType::SINGLE_SWITCH_MANY_GPU;
    else if (opt.scenario == "TWO_SWITCH_REDUNDANT") st = ScenarioType::TWO_SWITCH_REDUNDANT;
    else if (opt.scenario == "TWO_SWITCH_LINEAR") st = ScenarioType::TWO_SWITCH_LINEAR;
    else if (opt.scenario == "DENSE_MULTI_SWITCH") st = ScenarioType::DENSE_MULTI_SWITCH;
    else if (opt.scenario == "PARTITION_SPLIT") st = ScenarioType::PARTITION_SPLIT;
    else if (opt.scenario == "DEGRADED_PORT") st = ScenarioType::DEGRADED_PORT;
    else if (opt.scenario == "PARALLEL_LINKS") st = ScenarioType::PARALLEL_LINKS;
    else if (opt.scenario == "EMPTY") st = ScenarioType::EMPTY;
    (void)applyScenarioToRegistry(reg, st);
}

void printTextHeader(const char* title, const Options& opt) {
    std::printf("== NVSwitch Fabric | %s | scenario=%s | provenance=SYNTHETIC ==\n", title, opt.scenario.c_str());
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--scenario" && i+1 < argc) opt.scenario = argv[++i];
        else if (a == "--json") opt.json = true;
        else if (a == "--persisted" && i+1 < argc) opt.persisted = argv[++i];
        else if (a == "--nvidia") opt.nvidia = true;
        else if (a == "--cuda") opt.cuda = true;
        else if (a == "--a" && i+1 < argc) opt.a = DeviceId{std::strtoull(argv[++i], nullptr, 10)};
        else if (a == "--b" && i+1 < argc) opt.b = DeviceId{std::strtoull(argv[++i], nullptr, 10)};
        else if (!a.empty() && a[0]=='-') opt.action = a.substr(2);
    }

    FabricRegistry reg;
    if (opt.nvidia) {
        NvidiaBackend nb;
        auto ir = nb.initialize();
        if (!ir.ok()) { std::fprintf(stderr, "NVML init failed: %s\n", ir.error().message.c_str()); }
        else {
            auto cap = nb.capabilities();
            std::printf("backend=%s provenance=%s nvSwitchPresent=%d\n", cap.name.c_str(), toString(cap.provenance), cap.nvSwitchPresent?1:0);
            auto sc = nb.scan();
            if (sc.ok()) {
                FabricRegistry::BulkEdit e; e.devices = sc.value().devices;
                (void)reg.applyBulk(e);
            } else std::fprintf(stderr, "NVML scan failed: %s\n", sc.error().message.c_str());
        }
        nb.shutdown();
    } else if (!opt.persisted.empty()) {
        auto r = FabricRegistry::restoreFrom(opt.persisted);
        if (!r.ok()) { std::fprintf(stderr, "restore error: %s\n", r.error().message.c_str()); return 2; }
        // Rebuild reg from restored snapshot (copy topology).
        auto snap = r.value()->snapshot();
        FabricRegistry::BulkEdit e;
        for (auto& [id,d] : snap->devices()) e.devices.push_back(d);
        for (auto& [id,s] : snap->switches()) e.switches.push_back(s);
        for (auto& [id,p] : snap->ports()) e.ports.push_back(p);
        for (auto& [id,l] : snap->links()) e.links.push_back(l);
        for (auto& [id,pt] : snap->partitions()) e.partitions.push_back(pt);
        reg.applyBulk(e);
    } else {
        buildRegistry(reg, opt);
    }

    if (opt.cuda) {
        auto r = runCudaProof();
        if (opt.json) {
            Json j; j.beginObj(); bool f=true;
            j.sep(f); j.key("cudaPresent"); j.boolj(r.value().cudaPresent);
            j.sep(f); j.key("deviceDiscovery"); j.boolj(r.value().deviceDiscovery);
            j.sep(f); j.key("allocation"); j.boolj(r.value().allocation);
            j.sep(f); j.key("kernel"); j.boolj(r.value().kernel);
            j.sep(f); j.key("cpuParity"); j.boolj(r.value().cpuParity);
            j.sep(f); j.key("baselineReturned"); j.boolj(r.value().baselineReturned);
            j.sep(f); j.key("deviceCount"); j.u64j(r.value().deviceCount);
            j.sep(f); j.key("computeCapability"); j.str(std::to_string(r.value().computeCapabilityMajor)+"."+std::to_string(r.value().computeCapabilityMinor));
            j.sep(f); j.key("deviceName"); j.str(r.value().deviceName);
            j.sep(f); j.key("detail"); j.str(r.value().detail);
            j.endObj(); std::printf("%s\n", j.s.c_str());
        } else {
            std::printf("CUDA proof: present=%d discovery=%d alloc=%d kernel=%d parity=%d baseline=%d count=%d cc=%d.%d name=%s\n",
                r.value().cudaPresent, r.value().deviceDiscovery, r.value().allocation, r.value().kernel,
                r.value().cpuParity, r.value().baselineReturned, r.value().deviceCount,
                r.value().computeCapabilityMajor, r.value().computeCapabilityMinor, r.value().deviceName);
        }
        return 0;
    }

    auto snap = reg.snapshot();

    if (opt.action == "topology") {
        if (opt.json) {
            Json j; j.beginObj(); bool f=true;
            j.sep(f); j.key("provenance"); j.str(toString(snap->provenance()));
            j.sep(f); j.key("topologyGen"); j.u64j(snap->topologyGen().get());
            j.sep(f); j.key("fabricGen"); j.u64j(snap->fabricGen().get());
            j.sep(f); j.key("devices"); j.u64j(snap->deviceCount());
            j.sep(f); j.key("switches"); j.u64j(snap->switchCount());
            j.sep(f); j.key("ports"); j.u64j(snap->portCount());
            j.sep(f); j.key("links"); j.u64j(snap->linkCount());
            j.sep(f); j.key("partitions"); j.u64j(snap->partitionCount());
            j.endObj(); std::printf("%s\n", j.s.c_str());
        } else {
            printTextHeader("topology", opt);
            std::printf("  topologyGen=%llu fabricGen=%llu provenance=%s\n",
                (unsigned long long)snap->topologyGen().get(), (unsigned long long)snap->fabricGen().get(), toString(snap->provenance()));
            std::printf("  devices=%zu switches=%zu ports=%zu links=%zu partitions=%zu\n",
                snap->deviceCount(), snap->switchCount(), snap->portCount(), snap->linkCount(), snap->partitionCount());
        }
        return 0;
    }
    if (opt.action == "devices" || opt.action == "switches" || opt.action == "ports" ||
        opt.action == "links" || opt.action == "partitions" || opt.action == "health") {
        if (opt.action == "devices") {
            auto& m = snap->devices();
            for (auto& [id,d] : m) {
                std::printf("device %llu product=%s gen=%s state=%s prov=%s src=%s\n",
                    (unsigned long long)id.get(), d.product.c_str(), d.generation.toString().c_str(), toString(d.state), toString(d.provenance), toString(d.source));
            }
        } else if (opt.action == "switches") {
            for (auto& [id,s] : snap->switches())
                std::printf("switch %llu gen=%llu genName=%s ports=%u state=%s prov=%s\n",
                    (unsigned long long)id.get(), (unsigned long long)s.gen.get(), s.generationName.c_str(), s.portCount, toString(s.state), toString(s.provenance));
        } else if (opt.action == "ports") {
            for (auto& [id,p] : snap->ports())
                std::printf("port %llu.%u gen=%llu attached=%d state=%s prov=%s\n",
                    (unsigned long long)id.sw.get(), id.index, (unsigned long long)p.gen.get(), p.attachedKnown?1:0, toString(p.state), toString(p.provenance));
        } else if (opt.action == "links") {
            for (auto& [id,l] : snap->links())
                std::printf("link %llu root=%llu port=%llu.%u state=%s degraded=%d prov=%s\n",
                    (unsigned long long)id.get(), (unsigned long long)l.rootSwitch.get(), (unsigned long long)l.port.sw.get(), l.port.index, toString(l.state), l.degraded?1:0, toString(l.provenance));
        } else if (opt.action == "partitions") {
            for (auto& [id,p] : snap->partitions())
                std::printf("partition %s gen=%llu members=%zu state=%s prov=%s\n",
                    p.id.value.c_str(), (unsigned long long)p.gen.get(), p.members.size(), toString(p.state), toString(p.provenance));
        }
        return 0;
    }
    if (opt.action == "reach") {
        auto rr = reg.reachability(opt.a, opt.b);
        std::printf("reachability(%llu -> %llu) = %s\n", (unsigned long long)opt.a.get(), (unsigned long long)opt.b.get(), toString(rr));
        return 0;
    }
    if (opt.action == "paths" || opt.action == "route") {
        RoutePolicy pol;
        if (opt.action == "route") {
            auto d = reg.route(opt.a, opt.b, pol);
            std::printf("route(%llu -> %llu) outcome=%s auth=%d reason=%s\n",
                (unsigned long long)opt.a.get(), (unsigned long long)opt.b.get(), toString(d.value().outcome), d.value().authoritative?1:0, d.value().outcomeReason.c_str());
            if (d.value().selected) std::printf("  selected=%s\n", d.value().selected->canonicalKey().c_str());
            std::printf("  candidates=%zu\n", d.value().candidatePaths.size());
            for (auto& c : d.value().candidatePaths) std::printf("    %s state=%s\n", c.canonicalKey().c_str(), toString(c.state));
            return 0;
        }
        auto paths = reg.paths(opt.a, opt.b);
        std::printf("paths(%llu -> %llu) count=%zu\n", (unsigned long long)opt.a.get(), (unsigned long long)opt.b.get(), paths.size());
        for (auto& p : paths) std::printf("  %s state=%s\n", p.canonicalKey().c_str(), toString(p.state));
        return 0;
    }
    std::printf("unknown action '%s'\n", opt.action.c_str());
    return 1;
}