#pragma once
// POD boundary between the nvcc-compiled CUDA kernel module and the host
// (MSVC-compiled) C++ translation unit. No STL here so nvcc never touches the
// MSVC STL headers it cannot compile.
#ifdef __cplusplus
extern "C" {
#endif
struct NvfCudaKernelProof {
    int ok;
    int errorCode;
    int count;
    int major;
    int minor;
    unsigned long long bytes;
    char name[256];
    int allocOk;
    int h2dOk;
    int kernelOk;
    int d2hOk;
    int parityOk;
    int baselineOk;
};
int nvfCudaKernelProof(NvfCudaKernelProof* r);
#ifdef __cplusplus
}
#endif
