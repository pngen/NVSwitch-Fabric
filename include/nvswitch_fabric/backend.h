// NVSwitch Fabric - vendor-neutral backend interface.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include "nvswitch_fabric/identity.h"
#include "nvswitch_fabric/error.h"
#include "nvswitch_fabric/records.h"
#include "nvswitch_fabric/provenance.h"
#include "nvswitch_fabric/generation.h"
#include "nvswitch_fabric/evidence.h"

namespace nvswitch_fabric {

// A set of capabilities a backend declares (and can actually deliver).
struct BackendCapabilities {
    bool deviceDiscovery{false};
    bool switchDiscovery{false};
    bool portDiscovery{false};
    bool partitionDiscovery{false};
    bool linkDiscovery{false};
    bool switchManagement{false};
    bool telemetry{false};
    bool congestionCounters{false};
    bool nvSwitchPresent{false};
    Provenance provenance{Provenance::UNSUPPORTED};
    std::string name;

    std::vector<std::string> details() const {
        std::vector<std::string> d;
        auto add = [&](bool b, const char* s){ if (b) d.push_back(s); };
        add(deviceDiscovery,"deviceDiscovery");
        add(switchDiscovery,"switchDiscovery");
        add(portDiscovery,"portDiscovery");
        add(partitionDiscovery,"partitionDiscovery");
        add(linkDiscovery,"linkDiscovery");
        add(switchManagement,"switchManagement");
        add(telemetry,"telemetry");
        add(congestionCounters,"congestionCounters");
        return d;
    }
};

// Abstract backend. Lifecycle: initialize() -> scan(snapshot) -> shutdown().
// A backend returns raw observed records (with provenance) that the registry
// stages and commits transactionally.
class Backend {
public:
    virtual ~Backend() = default;

    virtual Result<void> initialize() = 0;
    virtual void shutdown() noexcept = 0;
    virtual BackendCapabilities capabilities() const = 0;

    // Refresh static capability + currently observable dynamic records.
    // Provides devices/switches/ports/links/partitions that this backend can
    // observe. Empty vectors are allowed for unsupported aspects.
    struct Scan {
        std::vector<DeviceRecord> devices;
        std::vector<SwitchRecord> switches;
        std::vector<SwitchPortRecord> ports;
        std::vector<LinkRecord> links;
        std::vector<FabricPartition> partitions;
        std::vector<SwitchHealthObservation> health;
        std::vector<CongestionObservation> congestion;
        std::string error;
    };
    virtual Result<Scan> scan() = 0;
};

} // namespace nvswitch_fabric
