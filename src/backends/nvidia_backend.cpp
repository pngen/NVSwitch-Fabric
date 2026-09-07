// NVSwitch Fabric - NVIDIA/NVML backend.
// Public supported NVML interfaces only. No internal/debug interfaces.
#include "nvswitch_fabric/backends/nvidia_backend.h"
#include "nvswitch_fabric/wire.h"
#include <cstring>

#ifdef NVSWITCH_FABRIC_HAVE_NVML
#include <nvml.h>
#define NVF_NVML_OK(r) ((r) == NVML_SUCCESS)
#endif

namespace nvswitch_fabric {

Result<void> NvidiaBackend::initialize() {
#ifdef NVSWITCH_FABRIC_HAVE_NVML
    nvmlReturn_t rc = nvmlInit();
    if (rc != NVML_SUCCESS) {
        // NVML present but failed to init (e.g. no NVIDIA driver).
        return Err(err(ErrorCode::BACKEND_UNAVAILABLE, "nvmlInit failed: " + std::string(nvmlErrorString(rc))));
    }
    unsigned int count = 0;
    if (!NVF_NVML_OK(nvmlDeviceGetCount(&count))) {
        nvmlShutdown();
        return Err(err(ErrorCode::BACKEND_ERROR, "nvmlDeviceGetCount failed"));
    }
    deviceCount_ = (int)count;
    inited_ = true;
    return Ok();
#else
    return Err(err(ErrorCode::BACKEND_UNAVAILABLE, "NVML not available"));
#endif
}

void NvidiaBackend::shutdown() noexcept {
#ifdef NVSWITCH_FABRIC_HAVE_NVML
    if (inited_) { nvmlShutdown(); inited_ = false; }
#endif
}

BackendCapabilities NvidiaBackend::capabilities() const {
    BackendCapabilities c;
    c.name = "nvidia-nvml";
#ifdef NVSWITCH_FABRIC_HAVE_NVML
    c.deviceDiscovery = true;
    c.switchDiscovery = false;     // No public NVSwitch-class enumeration on this platform.
    c.portDiscovery = false;
    c.partitionDiscovery = false;
    c.linkDiscovery = false;
    c.switchManagement = false;
    c.telemetry = false;
    c.congestionCounters = false;
    c.nvSwitchPresent = false;     // Honest: no NVSwitch observed.
    c.provenance = Provenance::REAL;
#else
    c.provenance = Provenance::UNSUPPORTED;
#endif
    return c;
}

Result<Backend::Scan> NvidiaBackend::scan() {
    Backend::Scan s;
#ifdef NVSWITCH_FABRIC_HAVE_NVML
    if (!inited_) return Err(err(ErrorCode::BACKEND_UNAVAILABLE, "not initialized"));
    for (int i = 0; i < deviceCount_; ++i) {
        nvmlDevice_t dev;
        if (!NVF_NVML_OK(nvmlDeviceGetHandleByIndex((unsigned)i, &dev))) return Err(err(ErrorCode::BACKEND_ERROR, "nvmlDeviceGetHandleByIndex"));
        DeviceRecord d;
        d.id = DeviceId{ (uint64_t)(i + 1) };
        d.present = true;
        d.vendor = "NVIDIA";
        char name[256] = {0};
        if (NVF_NVML_OK(nvmlDeviceGetName(dev, name, sizeof(name)))) d.product = name;
        char uuid[64] = {0};
        if (NVF_NVML_OK(nvmlDeviceGetUUID(dev, uuid, sizeof(uuid)))) d.backendId = uuid;
        int major = 0, minor = 0;
        if (NVF_NVML_OK(nvmlDeviceGetCudaComputeCapability(dev, &major, &minor))) {
            d.generation.name = "compute_" + std::to_string(major) + "." + std::to_string(minor);
            d.generation.observed = true;
        }
        d.state = EntityState::PRESENT;
        d.provenance = Provenance::REAL;
        d.source = EvidenceSource::NVML;
        // NVLink/NVSwitch presence: not surfaced by the supported public NVML APIs on
        // this platform. We never fabricate switch presence.
        d.attachedSwitchKnown = false;
        s.devices.push_back(d);
    }
    // No switch/port/link discovery is claimed on this platform.
    s.error.clear();
#else
    return Err(err(ErrorCode::BACKEND_UNAVAILABLE, "NVML not compiled"));
#endif
    return Ok(std::move(s));
}

} // namespace nvswitch_fabric