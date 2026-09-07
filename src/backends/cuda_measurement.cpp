// NVSwitch Fabric - real CUDA proof (host orchestration, MSVC-compiled).
// The device kernel lives in cuda_kernel.cu (nvcc-compiled); this host TU wires
// it up and reports honest provenance.
#include "nvswitch_fabric/backends/cuda_measurement.h"
#include "nvswitch_fabric/backends/cuda_kernel.h"
#include <cstring>

namespace nvswitch_fabric {

Result<CudaProofResult> runCudaProof() {
    CudaProofResult r;
#ifdef NVSWITCH_FABRIC_HAVE_CUDA
    NvfCudaKernelProof p{};
    int rc = nvfCudaKernelProof(&p);
    r.cudaPresent = (rc == 0);
    r.deviceDiscovery = (p.count > 0);
    r.allocation = p.allocOk != 0;
    r.kernel = p.kernelOk != 0;
    r.cpuParity = p.parityOk != 0;
    r.baselineReturned = p.baselineOk != 0;
    r.deviceCount = p.count;
    r.computeCapabilityMajor = p.major;
    r.computeCapabilityMinor = p.minor;
    std::snprintf(r.deviceName, sizeof(r.deviceName), "%s", p.name);
    if (!r.cudaPresent) r.detail = "CUDA runtime present but device unavailable";
    else if (r.cpuParity) r.detail = "CUDA proof OK: alloc/H2D/kernel/sync/D2H/parity/baseline";
    else r.detail = "CUDA kernel executed but parity mismatch";
#else
    r.detail = "CUDA not compiled/available";
#endif
    return Ok(r);
}

} // namespace nvswitch_fabric
