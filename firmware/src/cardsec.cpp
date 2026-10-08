#include "cardsec.h"

#include <mbedtls/md.h>

#include "util.h"

namespace cardsec {
namespace {

// HMAC-SHA256(secret, label || a || b), 32 bytes.
void hmac(const uint8_t secret[kSecretLen], const char* label, const uint8_t* a, size_t aLen,
          const uint8_t* b, size_t bLen, uint8_t out[32]) {
  uint8_t msg[11 + 10 + 96];
  const size_t labelLen = strlen(label);
  memcpy(msg, label, labelLen);
  memcpy(msg + labelLen, a, aLen);
  if (b && bLen) memcpy(msg + labelLen + aLen, b, bLen);
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), secret, kSecretLen, msg,
                  labelLen + aLen + bLen, out);
}

}  // namespace

bool parseSecret(const char* hex32, uint8_t out[kSecretLen]) {
  return hex::decode(hex32, out, kSecretLen);
}

Keys deriveKeys(const uint8_t secret[kSecretLen], const uint8_t* uid, uint8_t uidLen) {
  Keys k;
  uint8_t h[32];
  hmac(secret, "gapura-keyA", uid, uidLen, nullptr, 0, h);
  memcpy(k.a, h, 6);
  hmac(secret, "gapura-keyB", uid, uidLen, nullptr, 0, h);
  memcpy(k.b, h, 6);
  return k;
}

void computeCredMac(const uint8_t secret[kSecretLen], const uint8_t* uid, uint8_t uidLen,
                    const uint8_t data[kCredLen], uint8_t mac[16]) {
  uint8_t h[32];
  hmac(secret, "gapura-cred", uid, uidLen, data, kCredLen, h);
  memcpy(mac, h, 16);
}

bool macEqual(const uint8_t a[16], const uint8_t b[16]) {
  uint8_t diff = 0;  // constant time
  for (int i = 0; i < 16; i++) diff |= a[i] ^ b[i];
  return diff == 0;
}

String fingerprint(const uint8_t secret[kSecretLen]) {
  uint8_t h[32];
  const uint8_t none = 0;
  hmac(secret, "gapura-id", &none, 0, nullptr, 0, h);
  return String(hex::encode(h, 3).c_str());
}

void buildTrailer(const Keys& keys, uint8_t out[16]) {
  memcpy(out, keys.a, 6);
  memcpy(out + 6, kAccessBits, 4);
  memcpy(out + 10, keys.b, 6);
}

}  // namespace cardsec
