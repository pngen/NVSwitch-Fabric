// NVSwitch Fabric example: NVIDIA backend discovery + real CUDA proof.
#include <iostream>
#include "nvswitch_fabric/backends/nvidia_backend.h"
#include "nvswitch_fabric/backends/cuda_measurement.h"

using namespace nvswitch_fabric;

int main() {
    NvidiaBackend nb;
    auto ir = nb.initialize();
    if (ir.ok()) {
        auto cap = nb.capabilities();
        std::cout << "backend=" << cap.name << " provenance=" << toString(cap.provenance)
                  << " nvSwitchPresent=" << (cap.nvSwitchPresent?1:0) << "\n";
        auto sc = nb.scan();
        if (sc.ok()) {
            for (auto& d : sc.value().devices)
                std::cout << "  device " << d.id.get() << " " << d.product << " gen=" << d.generation.toString()
                          << " prov=" << toString(d.provenance) << " src=" << toString(d.source) << "\n";
        }
        nb.shutdown();
    } else {
        std::cout << "NVML unavailable: " << ir.error().message << "\n";
    }
    auto cu = runCudaProof();
    std::cout << "CUDA proof: present=" << cu.value().cudaPresent << " kernel=" << cu.value().kernel
              << " parity=" << cu.value().cpuParity << " count=" << cu.value().deviceCount
              << " cc=" << cu.value().computeCapabilityMajor << "." << cu.value().computeCapabilityMinor << "\n";
    return 0;
}
