// NVSwitch Fabric - framed TCP transport (length+CRC bounded, integrity checked).
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include "nvswitch_fabric/error.h"
#include "nvswitch_fabric/limits.h"

namespace nvswitch_fabric {
namespace rt {

// Protocol/transport message types (only what the boundary requires).
enum class MsgType : uint8_t {
    HELLO = 1,           // worker -> coordinator protocol handshake.
    REGISTER = 2,        // worker -> coordinator: workerId + boot.
    PUBLISH_TOPOLOGY = 3,// worker -> coordinator: a full topology snapshot.
    PUBLISH_MEASUREMENT = 4,
    QUERY_ROUTE = 5,     // client -> coordinator: route request.
    ROUTE_RESULT = 6,    // coordinator -> client: route decision.
    WORKER_READY = 7,    // worker -> coordinator: finished publishing, staying alive.
    PING = 8, PONG = 9,
    ERROR = 10,
};
inline bool validMsgType(uint8_t v) { return v >= 1 && v <= 10; }

// A single frame.
struct Frame {
    MsgType type{MsgType::HELLO};
    std::vector<uint8_t> payload;
};

// Frame wire layout (little-endian):
//   magic u32 | version u8 | type u8 | reserved u16 | length u32 | headerCrc u32
//   payload[length] | payloadCrc u32
constexpr uint32_t kFrameMagic = 0x4E564631u;      // "NVF1"
constexpr uint8_t kFrameVersion = 1;

// Encodes one frame (header + payload + CRCs).
std::vector<uint8_t> encodeFrame(const Frame& f);
// Decodes one frame from a contiguous buffer; rejects malformed frames.
Result<Frame> decodeFrame(const uint8_t* data, size_t len);
inline Result<Frame> decodeFrame(const std::vector<uint8_t>& v) { return decodeFrame(v.data(), v.size()); }

// Incremental frame reader: feeds raw bytes, yields complete frames.
class FrameStream {
public:
    bool feed(const uint8_t* data, size_t len, std::vector<Frame>& out);
    bool ok() const noexcept { return err_.code == ErrorCode::NONE; }
    const Error& error() const noexcept { return err_; }
private:
    std::vector<uint8_t> buf_;
    Error err_;
};

// Minimal blocking TCP socket (Winsock). Bounded by coordinator connection limit.
class TcpSocket {
public:
    TcpSocket() = default;
    ~TcpSocket();
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;
    TcpSocket(TcpSocket&& o) noexcept;
    TcpSocket& operator=(TcpSocket&& o) noexcept;
    Result<void> connectTo(const std::string& host, uint16_t port);
    Result<void> listenOn(uint16_t port);
    Result<TcpSocket> accept();
    Result<void> sendAll(const uint8_t* data, size_t len);
    Result<size_t> recvSome(uint8_t* data, size_t len);
    void shutdown() noexcept;
    void close() noexcept;
    bool valid() const noexcept { return fd_ != -1; }
    uint16_t localPort() const noexcept { return localPort_; }
private:
    intptr_t fd_ = -1;
    uint16_t localPort_ = 0;
};

// RAII Winsock init (ref-counted globally).
struct WsaGuard {
    WsaGuard();
    ~WsaGuard();
    bool ok() const noexcept { return ok_; }
private:
    bool ok_ = true;
};

} // namespace rt
} // namespace nvswitch_fabric
