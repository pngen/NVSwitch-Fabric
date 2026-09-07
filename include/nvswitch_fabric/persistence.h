// NVSwitch Fabric - persistence of durable fabric knowledge.
// Durable: topology/partition structure + generations + provenance. NOT
// persisted: live measurements, route-decision history, worker authority,
// congestion observations (dynamic evidence must always be revalidated).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "nvswitch_fabric/error.h"
#include "nvswitch_fabric/fabric_registry.h"
#include "nvswitch_fabric/wire.h"

namespace nvswitch_fabric {

class Persistence {
public:
    // Full file (magic + version + header crc + payload + payload crc).
    static Result<void> serializeToFile(const FabricRegistry& reg, const std::string& path);
    static Result<std::shared_ptr<FabricRegistry>> deserializeFromFile(const std::string& path);

    static Result<std::vector<uint8_t>> serialize(const FabricRegistry& reg);
    static Result<std::shared_ptr<FabricRegistry>> deserialize(const std::vector<uint8_t>& data);

    // Raw helpers used by the runtime transport as well.
    static Result<std::vector<uint8_t>> encodeTopology(const FabricTopology& topo);
    static Result<FabricTopology> decodeTopology(const uint8_t* p, size_t n);
    static Result<FabricTopology> decodeTopology(const std::vector<uint8_t>& v);

private:
    static void encodeTopologyPayload(ByteWriter& w, const FabricTopology& t);
    static Result<FabricTopology> decodeTopologyPayload(ByteReader& r);
};

} // namespace nvswitch_fabric
