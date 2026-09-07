// NVSwitch Fabric - strongly typed identities.
// Distinct identities are distinct C++ types; we never interchange raw
// strings or integers to mean different domain concepts.
#pragma once
#include <cstdint>
#include <compare>
#include <string>
#include <utility>

namespace nvswitch_fabric {

// A strongly-typed integer-backed identity. The Tag gives each domain
// concept its own unique type so identities cannot be silently mixed.
template <typename Tag, typename Rep = uint64_t>
struct Id {
    Rep value{};
    constexpr Id() = default;
    explicit constexpr Id(Rep v) noexcept : value(v) {}
    constexpr Rep get() const noexcept { return value; }
    explicit operator bool() const noexcept { return value != Rep{0}; }
    friend constexpr bool operator==(const Id&, const Id&) = default;
    constexpr auto operator<=>(const Id&) const = default;
};

// A string-backed identity (fabric/partition/snapshot names).
template <typename Tag>
struct StringId {
    std::string value;
    StringId() = default;
    explicit StringId(std::string v) : value(std::move(v)) {}
    bool empty() const noexcept { return value.empty(); }
    friend bool operator==(const StringId&, const StringId&) = default;
    auto operator<=>(const StringId&) const = default;
    friend bool operator<(const StringId& a, const StringId& b) { return a.value < b.value; }
};

// Tags (empty types).
struct DevTag{}; struct SwTag{}; struct FabTag{}; struct PartTag{};
struct SnapTag{}; struct LinkTag{}; struct PathTag{}; struct WorkerTag{};
struct RouteTag{}; struct ObsTag{}; struct MeasTag{}; struct PolicyTag{};
struct Bottag{}; struct RunTag{};

using DeviceId = Id<DevTag>;
using SwitchId = Id<SwTag>;
using FabricId = StringId<FabTag>;
using PartitionId = StringId<PartTag>;
using TopologySnapshotId = StringId<SnapTag>;
using LinkId = Id<LinkTag>;
using PathId = Id<PathTag>;
using WorkerId = StringId<WorkerTag>;
using RouteDecisionId = Id<RouteTag>;
using ObservationId = Id<ObsTag>;
using MeasurementId = Id<MeasTag>;
using PolicyId = StringId<PolicyTag>;

// Process incarnation / authority counters.
using CoordinatorEpoch = uint64_t;
using WorkerBootId = uint64_t;

// A port belongs to a switch (composite identity).
struct PortId {
    SwitchId sw;
    uint32_t index{};
    constexpr PortId() = default;
    constexpr PortId(SwitchId s, uint32_t i) noexcept : sw(s), index(i) {}
    friend constexpr bool operator==(const PortId&, const PortId&) = default;
    constexpr auto operator<=>(const PortId&) const = default;

    // Sentinel "unknown" port used when the far-side port of an inter-switch
    // half is not separately described.
    static constexpr PortId unknown() noexcept {
        return PortId{SwitchId{0xFFFFFFFFFFFFFFFFull}, 0xFFFFFFFFu};
    }
    constexpr bool isUnknown() const noexcept {
        return index == 0xFFFFFFFFu && sw.get() == 0xFFFFFFFFFFFFFFFFull;
    }
};

struct LinkEnd {
    enum class Kind : uint8_t { Device, Switch } kind{};
    DeviceId device;
    SwitchId sw;
    LinkEnd() = default;
    explicit LinkEnd(Kind k, DeviceId d, SwitchId s) noexcept : kind(k), device(d), sw(s) {}
    static LinkEnd toDevice(DeviceId d) noexcept { return LinkEnd{Kind::Device, d, SwitchId{}}; }
    static LinkEnd toSwitch(SwitchId s) noexcept { return LinkEnd{Kind::Switch, DeviceId{}, s}; }
    friend constexpr bool operator==(const LinkEnd&, const LinkEnd&) = default;
    constexpr auto operator<=>(const LinkEnd&) const = default;
};

} // namespace nvswitch_fabric
