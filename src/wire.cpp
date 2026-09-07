// NVSwitch Fabric - wire codec implementation.
#include "nvswitch_fabric/wire.h"
#include <cstring>

namespace nvswitch_fabric {

uint32_t crc32(const uint8_t* data, size_t len) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void ByteWriter::u8(uint8_t v) { v_.push_back(v); }
void ByteWriter::u16(uint16_t v) {
    v_.push_back((uint8_t)(v & 0xFF));
    v_.push_back((uint8_t)((v >> 8) & 0xFF));
}
void ByteWriter::u32(uint32_t v) {
    for (int i = 0; i < 4; ++i) v_.push_back((uint8_t)((v >> (8 * i)) & 0xFF));
}
void ByteWriter::u64(uint64_t v) {
    for (int i = 0; i < 8; ++i) v_.push_back((uint8_t)((v >> (8 * i)) & 0xFF));
}
void ByteWriter::i64(int64_t v) { u64((uint64_t)v); }
void ByteWriter::str(const std::string& s) {
    u32((uint32_t)s.size());
    v_.insert(v_.end(), s.begin(), s.end());
}
void ByteWriter::bytes(const uint8_t* p, size_t n) { v_.insert(v_.end(), p, p + n); }

bool ByteReader::u8(uint8_t& v) {
    if (pos_ + 1 > n_) return fail("overrun reading u8");
    v = p_[pos_++]; return true;
}
bool ByteReader::u16(uint16_t& v) {
    if (pos_ + 2 > n_) return fail("overrun reading u16");
    v = (uint16_t)(p_[pos_] | (p_[pos_ + 1] << 8)); pos_ += 2; return true;
}
bool ByteReader::u32(uint32_t& v) {
    if (pos_ + 4 > n_) return fail("overrun reading u32");
    v = 0; for (int i = 0; i < 4; ++i) v |= ((uint32_t)p_[pos_ + i]) << (8 * i); pos_ += 4; return true;
}
bool ByteReader::u64(uint64_t& v) {
    if (pos_ + 8 > n_) return fail("overrun reading u64");
    v = 0; for (int i = 0; i < 8; ++i) v |= ((uint64_t)p_[pos_ + i]) << (8 * i); pos_ += 8; return true;
}
bool ByteReader::i64(int64_t& v) { uint64_t t; if (!u64(t)) return false; v = (int64_t)t; return true; }
bool ByteReader::str(std::string& s) {
    uint32_t len; if (!u32(len)) return false;
    if ((size_t)len > (n_ - pos_)) return fail("string length exceeds remaining");
    if ((size_t)len > (64u * 1024u * 1024u)) return fail("string length absurd");
    s.assign((const char*)(p_ + pos_), len); pos_ += len; return true;
}
bool ByteReader::bytes(uint8_t* out, size_t n) {
    if (n > (n_ - pos_)) return fail("bytes length exceeds remaining");
    if (out) std::memcpy(out, p_ + pos_, n); pos_ += n; return true;
}
bool ByteReader::skip(size_t n) {
    if (n > (n_ - pos_)) return fail("skip exceeds remaining");
    pos_ += n; return true;
}

} // namespace nvswitch_fabric
