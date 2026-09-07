// NVSwitch Fabric - version.
#pragma once
#include <string>
namespace nvswitch_fabric {
inline constexpr int kVFSemVerMajor = 1;
inline constexpr int kVFSemVerMinor = 0;
inline constexpr int kVFSemVerPatch = 0;
inline constexpr const char* kVersionString = "1.0.0";

// Vendor-neutral protocol version for the multiprocess wire format.
inline constexpr uint8_t kWireProtocolVersion = 1;
// Persistence format version for durable fabric files.
inline constexpr uint32_t kPersistenceFormatVersion = 1;
inline const char* productName() { return "NVSwitch Fabric"; }
inline const char* vendorName() { return "Summon Software Labs"; }
}
