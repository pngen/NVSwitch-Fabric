#pragma once
#include <cstdint>
#include "nvswitch_fabric/error.h"
namespace nvswitch_fabric {
// Real CUDA proof: device discovery, real allocation, real kernel, CPU parity,
// and device-memory return to baseline. On unsupported/missing CUDA, the result
// carries provenance=UNSUPPORTED and succeeded=false.
struct CudaProofResult {
    bool cudaPresent{false};
    bool deviceDiscovery{false};
    bool allocation{false};
    bool kernel{false};
    bool cpuParity{false};
    bool baselineReturned{false};
    int deviceCount{0};
    int computeCapabilityMajor{0};
    int computeCapabilityMinor{0};
    char deviceName[256]{};
    std::string detail;
};
Result<CudaProofResult> runCudaProof();
}
