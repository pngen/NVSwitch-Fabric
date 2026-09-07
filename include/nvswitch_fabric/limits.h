// NVSwitch Fabric - explicit resource bounds.
#pragma once
#include <cstdint>
namespace nvswitch_fabric {
namespace limits {
inline constexpr uint32_t kMaxDevices        = 1024;
inline constexpr uint32_t kMaxSwitches       = 256;
inline constexpr uint32_t kMaxPortsPerSwitch = 256;
inline constexpr uint32_t kMaxPorts          = 65536;
inline constexpr uint32_t kMaxLinks          = 131072;
inline constexpr uint32_t kMaxPartitions     = 256;
inline constexpr uint32_t kMaxTopoNodes      = 16384;
inline constexpr uint32_t kMaxTopoEdges      = 262144;
inline constexpr uint32_t kMaxCandidatePaths = 65536;
inline constexpr uint32_t kMaxPathDepth      = 16;
inline constexpr uint32_t kMaxMeasurements   = 262144;
inline constexpr uint32_t kMaxWorkers        = 256;
inline constexpr uint32_t kMaxConnections    = 256;
inline constexpr uint32_t kMaxFrameBytes     = 16u * 1024u * 1024u;
inline constexpr uint32_t kMaxHistory        = 1u << 20;   // 1M trailing records bounded
inline constexpr uint32_t kMaxThreads        = 64;
inline constexpr uint64_t kMaxPersistenceBytes = 256ull * 1024ull * 1024ull;
inline constexpr uint32_t kMaxFixtureEntities = 8192;
} // namespace limits
} // namespace nvswitch_fabric
