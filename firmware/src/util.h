// Pure helpers (no Arduino dependency) so they can be unit-tested natively.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>

namespace soc {

struct Point {
  float v;
  uint8_t pct;
};

// Typical 1S Li-ion open-circuit curve, descending voltage.
constexpr Point kCurve[] = {
    {4.20f, 100}, {4.15f, 95}, {4.11f, 90}, {4.08f, 85}, {4.02f, 80},
    {3.98f, 75},  {3.95f, 70}, {3.91f, 65}, {3.87f, 60}, {3.85f, 55},
    {3.84f, 50},  {3.82f, 45}, {3.80f, 40}, {3.79f, 35}, {3.77f, 30},
    {3.75f, 25},  {3.73f, 20}, {3.71f, 15}, {3.69f, 10}, {3.61f, 5},
    {3.27f, 0},
};

inline float fromVoltage(float v) {
  constexpr size_t n = sizeof(kCurve) / sizeof(kCurve[0]);
  if (v >= kCurve[0].v) return 100.0f;
  if (v <= kCurve[n - 1].v) return 0.0f;
  for (size_t i = 1; i < n; i++) {
    if (v >= kCurve[i].v) {
      const Point& hi = kCurve[i - 1];
      const Point& lo = kCurve[i];
      return lo.pct + (v - lo.v) * (hi.pct - lo.pct) / (hi.v - lo.v);
    }
  }
  return 0.0f;
}

}  // namespace soc

namespace mifare {

constexpr uint8_t kBlockCount = 64;  // MIFARE Classic 1K
constexpr uint8_t kSectorCount = 16;
constexpr uint8_t kBlockSize = 16;

inline bool isTrailer(uint8_t block) { return (block & 0x03) == 0x03; }
// Block 0 holds the UID; trailers hold keys and access bits.
inline bool isWritable(uint8_t block) {
  return block != 0 && block < kBlockCount && !isTrailer(block);
}

}  // namespace mifare

namespace hex {

inline int nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Decodes exactly `len` bytes from 2*len hex chars. False on bad input.
inline bool decode(const char* s, uint8_t* out, size_t len) {
  if (!s) return false;
  for (size_t i = 0; i < len; i++) {
    const int hi = nibble(s[2 * i]);
    if (hi < 0) return false;
    const int lo = nibble(s[2 * i + 1]);
    if (lo < 0) return false;
    out[i] = static_cast<uint8_t>(hi << 4 | lo);
  }
  return s[2 * len] == '\0';
}

inline std::string encode(const uint8_t* data, size_t len) {
  static const char kHex[] = "0123456789ABCDEF";
  std::string s;
  s.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    s += kHex[data[i] >> 4];
    s += kHex[data[i] & 0x0F];
  }
  return s;
}

}  // namespace hex

// Reader hardware fitted (config `reader.mode`, applied after reboot).
// 13.56 MHz: RC522 or PN532 (MIFARE commands, card security); 125 kHz: RDM6300
// (EM4100 UID only). Combined modes share time slots, one antenna on at a time.
namespace reader {

enum class Mode : uint8_t { kRc522, kPn532, kRdm6300, kRc522Rdm6300, kPn532Rdm6300 };

struct ModeName {
  Mode mode;
  const char* name;
};
constexpr ModeName kModes[] = {
    {Mode::kRc522, "rc522"},
    {Mode::kPn532, "pn532"},
    {Mode::kRdm6300, "rdm6300"},
    {Mode::kRc522Rdm6300, "rc522+rdm6300"},
    {Mode::kPn532Rdm6300, "pn532+rdm6300"},
};

inline const char* toString(Mode m) {
  for (const ModeName& n : kModes) {
    if (n.mode == m) return n.name;
  }
  return "rc522";
}

inline bool parse(const char* s, Mode& out) {
  if (!s) return false;
  for (const ModeName& n : kModes) {
    if (strcmp(n.name, s) == 0) {
      out = n.mode;
      return true;
    }
  }
  return false;
}

// Stored value from NVS; anything unknown falls back to the RC522.
inline Mode fromIndex(uint8_t i) {
  return i <= static_cast<uint8_t>(Mode::kPn532Rdm6300) ? static_cast<Mode>(i) : Mode::kRc522;
}

inline bool hasRc522(Mode m) { return m == Mode::kRc522 || m == Mode::kRc522Rdm6300; }
inline bool hasPn532(Mode m) { return m == Mode::kPn532 || m == Mode::kPn532Rdm6300; }
inline bool hasRdm6300(Mode m) {
  return m == Mode::kRdm6300 || m == Mode::kRc522Rdm6300 || m == Mode::kPn532Rdm6300;
}

}  // namespace reader

// RDM6300 frame: 0x02, 10 ASCII hex (version byte + 32-bit tag id), 2 ASCII hex
// checksum (XOR of the 5 bytes), 0x03.
namespace em4100 {

constexpr size_t kFrameLen = 14;
constexpr size_t kIdLen = 5;

inline bool parseFrame(const uint8_t* f, uint8_t out[kIdLen]) {
  if (f[0] != 0x02 || f[kFrameLen - 1] != 0x03) return false;
  uint8_t bytes[kIdLen + 1];
  for (size_t i = 0; i < kIdLen + 1; i++) {
    const int hi = hex::nibble(static_cast<char>(f[1 + 2 * i]));
    const int lo = hex::nibble(static_cast<char>(f[2 + 2 * i]));
    if (hi < 0 || lo < 0) return false;
    bytes[i] = static_cast<uint8_t>(hi << 4 | lo);
  }
  uint8_t x = 0;
  for (size_t i = 0; i < kIdLen; i++) x ^= bytes[i];
  if (x != bytes[kIdLen]) return false;
  memcpy(out, bytes, kIdLen);
  return true;
}

}  // namespace em4100
