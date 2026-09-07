// NVSwitch Fabric - typed, monotonic generations.
#pragma once
#include <cstdint>
#include <compare>

namespace nvswitch_fabric {

template <typename Tag>
struct Gen {
    uint64_t value{};
    constexpr Gen() = default;
    explicit constexpr Gen(uint64_t v) noexcept : value(v) {}
    constexpr uint64_t get() const noexcept { return value; }
    constexpr Gen next() const noexcept { return Gen{value + 1}; }
    friend constexpr bool operator==(const Gen&, const Gen&) = default;
    constexpr auto operator<=>(const Gen&) const = default;
};

struct FabGenTag{}; struct TopoGenTag{}; struct SwGenTag{}; struct PortGenTag{};
struct PartGenTag{}; struct LinkGenTag{}; struct RouteGenTag{};

using FabricGeneration = Gen<FabGenTag>;
using TopologyGeneration = Gen<TopoGenTag>;
using SwitchGeneration = Gen<SwGenTag>;
using PortGeneration = Gen<PortGenTag>;
using PartitionGeneration = Gen<PartGenTag>;
using LinkGeneration = Gen<LinkGenTag>;
using RouteGeneration = Gen<RouteGenTag>;

} // namespace nvswitch_fabric
