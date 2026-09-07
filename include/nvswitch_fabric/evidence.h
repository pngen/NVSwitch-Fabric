// NVSwitch Fabric - dynamic evidence: measurement, congestion, health, freshness.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include "nvswitch_fabric/identity.h"
#include "nvswitch_fabric/generation.h"
#include "nvswitch_fabric/provenance.h"
#include "nvswitch_fabric/state.h"

namespace nvswitch_fabric {

// The strongest provable statement about a measured transfer.
enum class MeasurementKind : uint8_t {
    REAL_GPU_PEER_TRANSFER_NVSWITCH_TOPOLOGY_PRESENT = 0,
    REAL_GPU_PEER_TRANSFER = 1,
    REAL_GPU_SINGLE_DEVICE = 2,
    SYNTHETIC_FIXTURE = 3,
    UNSUPPORTED = 4,
};

struct Measurement {
    MeasurementId id;
    MeasurementId runId;
    DeviceId source;
    DeviceId destination;
    bool sourceKnown{false};
    bool destinationKnown{false};
    uint64_t payloadBytes{0};
    uint64_t iterations{0};
    uint64_t elapsedNanos{0};
    bool elapsedKnown{false};
    std::string syncMethod;             // e.g. "cudaStreamSynchronize".
    bool integrityVerified{false};
    bool matched{false};                // measurement still fresh/current.
    MeasurementKind kind{MeasurementKind::UNSUPPORTED};
    TopologyGeneration topologyGen;
    WorkerId worker;
    bool workerKnown{false};
    WorkerBootId workerBoot{0};
    Provenance provenance{Provenance::UNSUPPORTED};
    EvidenceSource evidenceSource{EvidenceSource::UNKNOWN};

    double bytesPerSec() const {
        if (!elapsedKnown || elapsedNanos == 0) return 0.0;
        return ((double)payloadBytes * iterations * 1e9) / (double)elapsedNanos;
    }
    double latencyUsPerOp() const {
        if (!elapsedKnown || iterations == 0) return 0.0;
        return ((double)elapsedNanos / (double)iterations) / 1000.0;
    }
};

struct LinkDegradation {
    LinkId link;
    bool linkKnown{false};
    bool degraded{false};
    double degradationFactor{0.0};      // ratio of current to nominal, <=1 when degraded.
    bool factorKnown{false};
    std::string reason;
    Provenance provenance{Provenance::UNSUPPORTED};
    EvidenceSource source{EvidenceSource::UNKNOWN};
};

struct CongestionObservation {
    CongestionEvidence classification{CongestionEvidence::NO_CONGESTION_EVIDENCE};
    std::vector<LinkDegradation> links;
    std::string explanation;
    WorkerId worker;
    bool workerKnown{false};
    WorkerBootId workerBoot{0};
    Provenance provenance{Provenance::UNSUPPORTED};
    EvidenceSource source{EvidenceSource::UNKNOWN};
    TopologyGeneration topologyGen;
    bool matched{false};
};

struct SwitchHealthObservation {
    SwitchId sw;
    EntityState state{EntityState::UNKNOWN};
    std::string reason;
    WorkerId worker;
    bool workerKnown{false};
    WorkerBootId workerBoot{0};
    Provenance provenance{Provenance::UNSUPPORTED};
    EvidenceSource source{EvidenceSource::UNKNOWN};
    TopologyGeneration topologyGen;
    bool matched{false};
};

} // namespace nvswitch_fabric
