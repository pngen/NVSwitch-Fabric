// NVSwitch Fabric - real CUDA kernel module (nvcc compiled, no STL).
#include "nvswitch_fabric/backends/cuda_kernel.h"
#include <cuda_runtime.h>
#include <cmath>
#include <cstdio>

namespace {
constexpr int kCount = 1 << 20;   // 1M elements.
__global__ void vadd(const float* a, const float* b, float* c, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) c[i] = a[i] + b[i];
}
} // namespace

extern "C" int nvfCudaKernelProof(NvfCudaKernelProof* r) {
    if (!r) return -1;
    *r = NvfCudaKernelProof{};
    r->bytes = (unsigned long long)kCount * sizeof(float);
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess || count <= 0) { r->ok = 0; return 0; }
    r->count = count;
    cudaDeviceProp prop{};
    if (cudaGetDeviceProperties(&prop, 0) == cudaSuccess) { r->major = prop.major; r->minor = prop.minor; std::snprintf(r->name, sizeof(r->name), "%s", prop.name); }
    float *a=nullptr,*b=nullptr,*c=nullptr;
    size_t bytes = (size_t)kCount * sizeof(float);
    if (cudaMalloc((void**)&a, bytes) != cudaSuccess || cudaMalloc((void**)&b, bytes) != cudaSuccess || cudaMalloc((void**)&c, bytes) != cudaSuccess) { r->errorCode = 1; cudaFree(a); cudaFree(b); cudaFree(c); r->ok = 0; return 0; }
    r->allocOk = 1;
    static float ha[kCount]; static float hb[kCount]; static float hc[kCount];
    for (int i = 0; i < kCount; ++i) { ha[i] = (float)(i % 17); hb[i] = (float)(i % 13); }
    if (cudaMemcpy(a, ha, bytes, cudaMemcpyHostToDevice) != cudaSuccess ||
        cudaMemcpy(b, hb, bytes, cudaMemcpyHostToDevice) != cudaSuccess) { r->errorCode = 2; cudaFree(a); cudaFree(b); cudaFree(c); r->ok = 0; return 0; }
    r->h2dOk = 1;
    vadd<<<(kCount + 255) / 256, 256>>>(a, b, c, kCount);
    if (cudaGetLastError() != cudaSuccess) { r->errorCode = 3; cudaFree(a); cudaFree(b); cudaFree(c); r->ok = 0; return 0; }
    if (cudaDeviceSynchronize() != cudaSuccess) { r->errorCode = 4; cudaFree(a); cudaFree(b); cudaFree(c); r->ok = 0; return 0; }
    r->kernelOk = 1;
    if (cudaMemcpy(hc, c, bytes, cudaMemcpyDeviceToHost) != cudaSuccess) { r->errorCode = 5; cudaFree(a); cudaFree(b); cudaFree(c); r->ok = 0; return 0; }
    r->d2hOk = 1;
    bool parity = true;
    for (int i = 0; i < kCount; ++i) { float expected = ha[i] + hb[i]; if (std::fabs(hc[i] - expected) > 1e-4f) { parity = false; break; } }
    r->parityOk = parity ? 1 : 0;
    cudaFree(a); cudaFree(b); cudaFree(c);
    r->baselineOk = (cudaDeviceReset() == cudaSuccess) ? 1 : 0;
    r->ok = 1;
    return 0;
}
