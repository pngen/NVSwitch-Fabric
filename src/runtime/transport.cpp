// NVSwitch Fabric - framed TCP transport (Winsock) implementation.
#include "nvswitch_fabric/runtime/transport.h"
#include "nvswitch_fabric/wire.h"

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include <cstring>
#include <cstdio>
#include <atomic>
#include <mutex>

namespace nvswitch_fabric {
namespace rt {

namespace {
void ensureWsa() {
#ifdef _WIN32
    static std::once_flag flag;
    std::call_once(flag, []{ WSADATA d; (void)WSAStartup(MAKEWORD(2,2), &d); });
#endif
}
} // namespace

WsaGuard::WsaGuard() {
    ensureWsa();
}
WsaGuard::~WsaGuard() {}

std::vector<uint8_t> encodeFrame(const Frame& f) {
    ByteWriter w;
    w.u32(kFrameMagic);
    w.u8(kFrameVersion);
    w.u8((uint8_t)f.type);
    w.u16(0);
    w.u32((uint32_t)f.payload.size());
    uint32_t hdrCrc = crc32(w.data().data(), w.size());
    w.u32(hdrCrc);
    w.raw(f.payload.data(), f.payload.size());
    uint32_t payCrc = crc32(f.payload.data(), f.payload.size());
    w.u32(payCrc);
    return w.data();
}

Result<Frame> decodeFrame(const uint8_t* data, size_t len) {
    constexpr size_t kHeader = 16;
    if (len < kHeader)
        return Err(err(ErrorCode::PROTOCOL_ERROR, "frame too short"));
    uint32_t magic = 0, length = 0, hdrCrc = 0;
    uint8_t ver = 0, type = 0;
    for (int i = 0; i < 4; ++i) magic |= ((uint32_t)data[i]) << (8 * i);
    ver = data[4]; type = data[5];
    for (int i = 0; i < 4; ++i) length |= ((uint32_t)data[8 + i]) << (8 * i);
    for (int i = 0; i < 4; ++i) hdrCrc |= ((uint32_t)data[12 + i]) << (8 * i);

    if (magic != kFrameMagic) return Err(err(ErrorCode::PROTOCOL_ERROR, "bad frame magic"));
    if (ver != kFrameVersion) return Err(err(ErrorCode::PROTOCOL_ERROR, "unsupported frame version"));
    if (!validMsgType(type)) return Err(err(ErrorCode::PROTOCOL_ERROR, "invalid frame type"));
    if (length > limits::kMaxFrameBytes) return Err(err(ErrorCode::RESOURCE_LIMIT, "frame length absurd"));
    if (crc32(data, 12) != hdrCrc) return Err(err(ErrorCode::INTEGRITY_FAILURE, "frame header crc"));
    size_t total = kHeader + (size_t)length + 4;
    if (total > len) return Err(err(ErrorCode::PROTOCOL_ERROR, "truncated frame"));
    const uint8_t* payload = data + kHeader;
    uint32_t payCrc = 0;
    for (int i = 0; i < 4; ++i) payCrc |= ((uint32_t)data[kHeader + length + i]) << (8 * i);
    if (crc32(payload, length) != payCrc) return Err(err(ErrorCode::INTEGRITY_FAILURE, "frame payload crc"));
    Frame f;
    f.type = (MsgType)type;
    f.payload.assign(payload, payload + length);
    return Ok(std::move(f));
}

bool FrameStream::feed(const uint8_t* data, size_t len, std::vector<Frame>& out) {
    if (err_.code != ErrorCode::NONE) return false;
    buf_.insert(buf_.end(), data, data + len);
    constexpr size_t kHeader = 16;
    for (;;) {
        if (buf_.size() < kHeader) break;
        uint32_t magic = 0, length = 0, hdrCrc = 0;
        uint8_t ver = 0, type = 0;
        for (int i = 0; i < 4; ++i) magic |= ((uint32_t)buf_[i]) << (8 * i);
        ver = buf_[4]; type = buf_[5];
        for (int i = 0; i < 4; ++i) length |= ((uint32_t)buf_[8 + i]) << (8 * i);
        for (int i = 0; i < 4; ++i) hdrCrc |= ((uint32_t)buf_[12 + i]) << (8 * i);
        if (magic != kFrameMagic) { err_ = err(ErrorCode::PROTOCOL_ERROR, "bad frame magic"); return false; }
        if (ver != kFrameVersion) { err_ = err(ErrorCode::PROTOCOL_ERROR, "unsupported frame version"); return false; }
        if (!validMsgType(type)) { err_ = err(ErrorCode::PROTOCOL_ERROR, "invalid frame type"); return false; }
        if (length > limits::kMaxFrameBytes) { err_ = err(ErrorCode::RESOURCE_LIMIT, "frame length absurd"); return false; }
        size_t total = kHeader + (size_t)length + 4;
        if (buf_.size() < total) break;
        if (crc32(buf_.data(), 12) != hdrCrc) { err_ = err(ErrorCode::INTEGRITY_FAILURE, "frame header crc"); return false; }
        const uint8_t* payload = buf_.data() + kHeader;
        uint32_t payCrc = 0;
        for (int i = 0; i < 4; ++i) payCrc |= ((uint32_t)buf_[kHeader + length + i]) << (8 * i);
        if (crc32(payload, length) != payCrc) { err_ = err(ErrorCode::INTEGRITY_FAILURE, "frame payload crc"); return false; }
        Frame f; f.type = (MsgType)type; f.payload.assign(payload, payload + length);
        out.push_back(std::move(f));
        buf_.erase(buf_.begin(), buf_.begin() + total);
    }
    return true;
}

namespace {
#ifdef _WIN32
using SockT = SOCKET;
constexpr SockT kInvalid = INVALID_SOCKET;
void closeSock(SOCKET& s) { if (s != INVALID_SOCKET) { closesocket(s); s = INVALID_SOCKET; } }
#else
using SockT = int;
constexpr SockT kInvalid = -1;
void closeSock(int& s) { if (s != -1) { close(s); s = -1; } }
#endif
} // namespace

TcpSocket::~TcpSocket() { close(); }
TcpSocket::TcpSocket(TcpSocket&& o) noexcept : fd_(o.fd_), localPort_(o.localPort_) { o.fd_ = kInvalid; }
TcpSocket& TcpSocket::operator=(TcpSocket&& o) noexcept {
    if (this != &o) { close(); fd_ = o.fd_; localPort_ = o.localPort_; o.fd_ = kInvalid; }
    return *this;
}
Result<void> TcpSocket::connectTo(const std::string& host, uint16_t port) {
    ensureWsa();
    fd_ = (intptr_t)socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ == (intptr_t)kInvalid) return Err(err(ErrorCode::BACKEND_ERROR, "socket"));
    char portstr[16]; std::snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);
    struct addrinfo hints{};
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), portstr, &hints, &res) != 0) { close(); return Err(err(ErrorCode::BACKEND_ERROR, "resolve host")); }
    int rc = connect((SockT)fd_, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);
    if (rc != 0) { close(); return Err(err(ErrorCode::BACKEND_ERROR, "connect")); }
    return Ok();
}
Result<void> TcpSocket::listenOn(uint16_t port) {
    ensureWsa();
    fd_ = (intptr_t)socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ == (intptr_t)kInvalid) return Err(err(ErrorCode::BACKEND_ERROR, "socket"));
    int one = 1;
    setsockopt((SockT)fd_, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); addr.sin_port = htons(port);
    if (bind((SockT)fd_, (sockaddr*)&addr, sizeof(addr)) != 0) { close(); return Err(err(ErrorCode::BACKEND_ERROR, "bind")); }
    if (listen((SockT)fd_, 16) != 0) { close(); return Err(err(ErrorCode::BACKEND_ERROR, "listen")); }
    sockaddr_in local{}; int len = sizeof(local); getsockname((SockT)fd_, (sockaddr*)&local, &len); localPort_ = ntohs(local.sin_port);
    return Ok();
}
Result<TcpSocket> TcpSocket::accept() {
    sockaddr_in peer{}; int len = sizeof(peer);
    SockT c = ::accept((SockT)fd_, (sockaddr*)&peer, &len);
    if (c == kInvalid) return Err(err(ErrorCode::BACKEND_ERROR, "accept"));
    TcpSocket s; s.fd_ = (intptr_t)c; s.localPort_ = localPort_; return Ok(std::move(s));
}
Result<void> TcpSocket::sendAll(const uint8_t* data, size_t len) {
    size_t off = 0;
    while (off < len) {
        int n = send((SockT)fd_, (const char*)(data + off), (int)(len - off), 0);
        if (n <= 0) { if (n == 0) return Err(err(ErrorCode::INTERNAL, "send closed")); return Err(err(ErrorCode::BACKEND_ERROR, "send")); }
        off += (size_t)n;
    }
    return Ok();
}
Result<size_t> TcpSocket::recvSome(uint8_t* data, size_t len) {
    int n = recv((SockT)fd_, (char*)data, (int)len, 0);
    if (n < 0) return Err(err(ErrorCode::BACKEND_ERROR, "recv"));
    return Ok((size_t)n);
}
void TcpSocket::shutdown() noexcept {
#ifdef _WIN32
    if (fd_ != (intptr_t)kInvalid) ::shutdown((SockT)fd_, SD_SEND);
#else
    if (fd_ != (intptr_t)kInvalid) ::shutdown((SockT)fd_, SHUT_WR);
#endif
}
void TcpSocket::close() noexcept {
#ifdef _WIN32
    if (fd_ != (intptr_t)kInvalid) { closesocket((SockT)fd_); fd_ = kInvalid; }
#else
    if (fd_ != (intptr_t)kInvalid) { close((SockT)fd_); fd_ = kInvalid; }
#endif
}

} // namespace rt
} // namespace nvswitch_fabric
