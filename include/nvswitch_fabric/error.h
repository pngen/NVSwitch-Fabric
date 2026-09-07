// NVSwitch Fabric - typed errors + Result<T>.
// We never reduce a meaningful failure to a bare generic string.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <variant>

namespace nvswitch_fabric {

enum class ErrorCode : uint8_t {
    NONE = 0,
    NOT_FOUND = 1,
    UNSUPPORTED = 2,
    INVALID_ARGUMENT = 3,
    INVALID_TOPOLOGY = 4,
    INVALID_PARTITION = 5,
    STALE_GENERATION = 6,
    STALE_SWITCH = 7,
    STALE_PORT = 8,
    STALE_PARTITION = 9,
    STALE_BOOT = 10,
    STALE_EPOCH = 11,
    STALE_MEASUREMENT = 12,
    REVALIDATION_REQUIRED = 13,
    BACKEND_UNAVAILABLE = 14,
    BACKEND_ERROR = 15,
    SWITCH_DOWN = 16,
    PORT_DOWN = 17,
    PARTITION_BLOCKED = 18,
    NO_PATH = 19,
    INSUFFICIENT_EVIDENCE = 20,
    INTEGRITY_FAILURE = 21,
    PROTOCOL_ERROR = 22,
    RESOURCE_LIMIT = 23,
    CANCELLED = 24,
    ALREADY_EXISTS = 25,
    INTERNAL = 26,
};

struct Error {
    ErrorCode code{ErrorCode::NONE};
    std::string message;

    Error() = default;
    explicit Error(ErrorCode c, std::string m = {}) : code(c), message(std::move(m)) {}
    bool ok() const noexcept { return code == ErrorCode::NONE; }
    explicit operator bool() const noexcept { return ok(); }
    friend bool operator==(const Error& a, const Error& b) {
        return a.code == b.code && a.message == b.message;
    }
};

inline const char* toString(ErrorCode c) noexcept {
    switch (c) {
        case ErrorCode::NONE: return "NONE";
        case ErrorCode::NOT_FOUND: return "NOT_FOUND";
        case ErrorCode::UNSUPPORTED: return "UNSUPPORTED";
        case ErrorCode::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case ErrorCode::INVALID_TOPOLOGY: return "INVALID_TOPOLOGY";
        case ErrorCode::INVALID_PARTITION: return "INVALID_PARTITION";
        case ErrorCode::STALE_GENERATION: return "STALE_GENERATION";
        case ErrorCode::STALE_SWITCH: return "STALE_SWITCH";
        case ErrorCode::STALE_PORT: return "STALE_PORT";
        case ErrorCode::STALE_PARTITION: return "STALE_PARTITION";
        case ErrorCode::STALE_BOOT: return "STALE_BOOT";
        case ErrorCode::STALE_EPOCH: return "STALE_EPOCH";
        case ErrorCode::STALE_MEASUREMENT: return "STALE_MEASUREMENT";
        case ErrorCode::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
        case ErrorCode::BACKEND_UNAVAILABLE: return "BACKEND_UNAVAILABLE";
        case ErrorCode::BACKEND_ERROR: return "BACKEND_ERROR";
        case ErrorCode::SWITCH_DOWN: return "SWITCH_DOWN";
        case ErrorCode::PORT_DOWN: return "PORT_DOWN";
        case ErrorCode::PARTITION_BLOCKED: return "PARTITION_BLOCKED";
        case ErrorCode::NO_PATH: return "NO_PATH";
        case ErrorCode::INSUFFICIENT_EVIDENCE: return "INSUFFICIENT_EVIDENCE";
        case ErrorCode::INTEGRITY_FAILURE: return "INTEGRITY_FAILURE";
        case ErrorCode::PROTOCOL_ERROR: return "PROTOCOL_ERROR";
        case ErrorCode::RESOURCE_LIMIT: return "RESOURCE_LIMIT";
        case ErrorCode::CANCELLED: return "CANCELLED";
        case ErrorCode::ALREADY_EXISTS: return "ALREADY_EXISTS";
        case ErrorCode::INTERNAL: return "INTERNAL";
    }
    return "NONE";
}

// ---------------------------------------------------------------------------
// Result<T>: either a value or a typed Error. void is specialized.
// ---------------------------------------------------------------------------
template <typename T>
struct Result {
    std::variant<T, Error> data;   // requires <variant>, included below.

    Result() : data(T{}) {}
    Result(T v) : data(std::move(v)) {}
    Result(Error e) : data(std::move(e)) {}

    bool ok() const noexcept { return std::holds_alternative<T>(data); }
    bool has_value() const noexcept { return ok(); }
    T& value() { return std::get<T>(data); }
    const T& value() const { return std::get<T>(data); }
    T&& move_value() { return std::get<T>(std::move(data)); }
    Error& error() { return std::get<Error>(data); }
    const Error& error() const { return std::get<Error>(data); }
    explicit operator bool() const noexcept { return ok(); }
};

template <>
struct Result<void> {
    Error err_;
    Result() = default;                       // success.
    Result(Error e) : err_(std::move(e)) {}   // failure.
    bool ok() const noexcept { return err_.code == ErrorCode::NONE; }
    bool has_value() const noexcept { return ok(); }
    void value() const noexcept {}
    Error& error() { return err_; }
    const Error& error() const { return err_; }
    explicit operator bool() const noexcept { return ok(); }
};

// ---------------------------------------------------------------------------
// Ok / Err wrappers: deduce the Result<T> from the return-context via an
// implicit conversion operator, so call sites read "return Ok(x);" / "return Err(e);".
// ---------------------------------------------------------------------------
template <typename T>
struct OkWrapper {
    T v;
    // Prefer the rvalue path (move) for move-only values; fall back to copy.
    template <typename U> operator Result<U>() && { return Result<U>(std::move(v)); }
    template <typename U> operator Result<U>() const& { return Result<U>(v); }
};
template <>
struct OkWrapper<void> {
    operator Result<void>() const { return Result<void>(); }
};
struct ErrWrapper {
    Error e;
    template <typename U> operator Result<U>() const { return Result<U>(e); }
};

template <typename T> OkWrapper<T> Ok(T v) { return OkWrapper<T>{std::move(v)}; }
inline OkWrapper<void> Ok() { return OkWrapper<void>{}; }
inline ErrWrapper Err(Error e) { return ErrWrapper{std::move(e)}; }
inline ErrWrapper Err(ErrorCode c, std::string m = {}) { return ErrWrapper{Error{c, std::move(m)}}; }

// Convenience error maker.
inline Error err(ErrorCode c, std::string m = {}) { return Error{c, std::move(m)}; }

} // namespace nvswitch_fabric
