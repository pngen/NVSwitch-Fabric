// NVSwitch Fabric - deterministic binary codec (wire + persistence).
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include "nvswitch_fabric/error.h"

namespace nvswitch_fabric {

// CRC32 (IEEE). Used for frame and payload integrity.
uint32_t crc32(const uint8_t* data, size_t len);
inline uint32_t crc32(const std::vector<uint8_t>& v) { return crc32(v.data(), v.size()); }

// A deterministic little-endian byte writer.
class ByteWriter {
public:
    void u8(uint8_t v);
    void u16(uint16_t v);
    void u32(uint32_t v);
    void u64(uint64_t v);
    void i64(int64_t v);
    void str(const std::string& s);           // u32 length + bytes.
    void bytes(const uint8_t* p, size_t n);
    void raw(const uint8_t* p, size_t n) { v_.insert(v_.end(), p, p + n); }
    const std::vector<uint8_t>& data() const noexcept { return v_; }
    size_t size() const noexcept { return v_.size(); }

private:
    std::vector<uint8_t> v_;
};

// A bounded little-endian byte reader. Reads return false on overrun/absurdity
// and set the reader's error. Use ok() to detect corruption.
class ByteReader {
public:
    ByteReader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    explicit ByteReader(const std::vector<uint8_t>& v) : p_(v.data()), n_(v.size()) {}

    bool u8(uint8_t& v);
    bool u16(uint16_t& v);
    bool u32(uint32_t& v);
    bool u64(uint64_t& v);
    bool i64(int64_t& v);
    bool str(std::string& s);
    bool bytes(uint8_t* out, size_t n);
    bool skip(size_t n);
    size_t remaining() const noexcept { return n_ - pos_; }
    size_t position() const noexcept { return pos_; }
    bool eof() const noexcept { return pos_ == n_; }
    bool ok() const noexcept { return ok_; }
    const std::string& error() const noexcept { return err_; }
    const uint8_t* data() const noexcept { return p_; }
    size_t size() const noexcept { return n_; }

private:
    bool fail(const char* msg) { ok_ = false; err_ = msg; return false; }
    const uint8_t* p_; size_t n_; size_t pos_ = 0; bool ok_ = true; std::string err_;
};

} // namespace nvswitch_fabric
