// NVSwitch Fabric - evidence provenance classification.
// Every hardware-facing result carries Provenance plus an EvidenceSource.
#pragma once
#include <cstdint>
#include <string>

namespace nvswitch_fabric {

enum class Provenance : uint8_t {
    REAL,          // genuinely observed from live hardware via supported APIs.
    SYNTHETIC,     // a constructed fixture; never hardware evidence.
    UNSUPPORTED    // the capability is not supported in this environment.
};

enum class EvidenceSource : uint8_t {
    UNKNOWN = 0,
    NVML = 1,
    CUDA_RUNTIME = 2,
    CUDA_DRIVER = 3,
    NVIDIA_FABRIC_MANAGER = 4,
    OPERATING_SYSTEM = 5,
    BENCHMARK = 6,
    PERSISTED = 7,
    DERIVED = 8,
    SYNTHETIC_FIXTURE = 9,
};

inline const char* toString(Provenance p) noexcept {
    switch (p) {
        case Provenance::REAL: return "REAL";
        case Provenance::SYNTHETIC: return "SYNTHETIC";
        case Provenance::UNSUPPORTED: return "UNSUPPORTED";
    }
    return "UNSUPPORTED";
}
inline const char* toString(EvidenceSource s) noexcept {
    switch (s) {
        case EvidenceSource::UNKNOWN: return "UNKNOWN";
        case EvidenceSource::NVML: return "NVML";
        case EvidenceSource::CUDA_RUNTIME: return "CUDA_RUNTIME";
        case EvidenceSource::CUDA_DRIVER: return "CUDA_DRIVER";
        case EvidenceSource::NVIDIA_FABRIC_MANAGER: return "NVIDIA_FABRIC_MANAGER";
        case EvidenceSource::OPERATING_SYSTEM: return "OPERATING_SYSTEM";
        case EvidenceSource::BENCHMARK: return "BENCHMARK";
        case EvidenceSource::PERSISTED: return "PERSISTED";
        case EvidenceSource::DERIVED: return "DERIVED";
        case EvidenceSource::SYNTHETIC_FIXTURE: return "SYNTHETIC_FIXTURE";
    }
    return "UNKNOWN";
}

} // namespace nvswitch_fabric
