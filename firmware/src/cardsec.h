// Card security: per-card sector keys and a signed credential, all derived from
// one secret shared by all readers (and the server). docs/PROTOCOL.md "Card security".
//
//   keyA = HMAC-SHA256(secret, "gapura-keyA" || UID)[0..6]   read-only access
//   keyB = HMAC-SHA256(secret, "gapura-keyB" || UID)[0..6]   write access
//   mac  = HMAC-SHA256(secret, "gapura-cred" || UID || data)[0..16]
//
// The credential (user text, up to 95 characters) is zero padded to 96 bytes and
// stored in blocks 4-6 and 8-10; block 12 holds the mac. Blocks 7, 11 and 15 are
// sector trailers carrying the derived keys.
#pragma once

#include <Arduino.h>

namespace cardsec {

constexpr size_t kSecretLen = 16;

// Data blocks: read with A or B, write with B only. Trailer: keys and access
// bits writable with B only, never readable. Last byte is the free user byte.
constexpr uint8_t kAccessBits[4] = {0x78, 0x77, 0x88, 0x69};
// Factory transport configuration: key A writes everything.
constexpr uint8_t kFactoryAccessBits[4] = {0xFF, 0x07, 0x80, 0x69};

// Credential layout.
struct CredSector {
  uint8_t first;    // first of three data blocks
  uint8_t trailer;
};
constexpr CredSector kCredSectors[2] = {{4, 7}, {8, 11}};
constexpr uint8_t kMacBlock = 12;
constexpr uint8_t kMacTrailer = 15;
constexpr size_t kCredLen = 96;
constexpr size_t kMaxCredential = 95;  // leaves a 0x00 terminator

struct Keys {
  uint8_t a[6];
  uint8_t b[6];
};

bool parseSecret(const char* hex32, uint8_t out[kSecretLen]);
Keys deriveKeys(const uint8_t secret[kSecretLen], const uint8_t* uid, uint8_t uidLen);
void computeCredMac(const uint8_t secret[kSecretLen], const uint8_t* uid, uint8_t uidLen,
                    const uint8_t data[kCredLen], uint8_t mac[16]);
bool macEqual(const uint8_t a[16], const uint8_t b[16]);
// Short public fingerprint to compare secrets between readers, e.g. "a1b2c3".
String fingerprint(const uint8_t secret[kSecretLen]);
// Builds the 16-byte sector trailer from the derived keys.
void buildTrailer(const Keys& keys, uint8_t out[16]);

}  // namespace cardsec
