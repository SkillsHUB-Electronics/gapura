#include <SPI.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "hf.h"

namespace {

// SPI byte that starts each transfer (PN532 user manual §6.2.5).
constexpr uint8_t kDataWrite = 0x01;
constexpr uint8_t kStatusRead = 0x02;
constexpr uint8_t kDataRead = 0x03;

constexpr uint8_t kHostToPn = 0xD4;
constexpr uint8_t kPnToHost = 0xD5;

constexpr uint8_t kCmdGetFirmwareVersion = 0x02;
constexpr uint8_t kCmdSamConfiguration = 0x14;
constexpr uint8_t kCmdPowerDown = 0x16;
constexpr uint8_t kCmdRfConfiguration = 0x32;
constexpr uint8_t kCmdInDataExchange = 0x40;
constexpr uint8_t kCmdInListPassiveTarget = 0x4A;
constexpr uint8_t kCmdInRelease = 0x52;

constexpr uint8_t kMifareAuthA = 0x60;
constexpr uint8_t kMifareAuthB = 0x61;
constexpr uint8_t kMifareRead = 0x30;
constexpr uint8_t kMifareWrite = 0xA0;

const SPISettings kSpi(1000000, LSBFIRST, SPI_MODE0);

}  // namespace

void Pn532Reader::begin_() {
  SPI.beginTransaction(kSpi);
  digitalWrite(pins::kRfidSs, LOW);
}

void Pn532Reader::end_() {
  digitalWrite(pins::kRfidSs, HIGH);
  SPI.endTransaction();
}

bool Pn532Reader::begin() {
  SPI.begin(pins::kRfidSck, pins::kRfidMiso, pins::kRfidMosi, pins::kRfidSs);
  pinMode(pins::kRfidSs, OUTPUT);
  digitalWrite(pins::kRfidSs, HIGH);
  // Chip select low wakes the PN532; the first command after power-up is
  // sometimes lost, so send one and ignore the answer.
  digitalWrite(pins::kRfidSs, LOW);
  delay(2);
  digitalWrite(pins::kRfidSs, HIGH);
  delay(10);
  uint8_t resp[4];
  command(kCmdGetFirmwareVersion, nullptr, 0, resp, sizeof(resp));
  if (command(kCmdGetFirmwareVersion, nullptr, 0, resp, sizeof(resp)) != 4 || resp[0] != 0x32) {
    return false;
  }
  version_ = static_cast<uint8_t>(resp[1] << 4 | (resp[2] & 0x0F));  // 0x16 = v1.6

  const uint8_t sam[] = {0x01, 0x14, 0x01};  // normal mode, 1 s timeout, use IRQ
  if (command(kCmdSamConfiguration, sam, sizeof(sam), resp, sizeof(resp)) < 0) return false;
  // MaxRetries: ATR 0xFF, PSL 1, passive activation 2 tries, so InListPassiveTarget
  // answers at once when no card is there instead of waiting forever.
  const uint8_t retries[] = {0x05, 0xFF, 0x01, 0x02};
  return command(kCmdRfConfiguration, retries, sizeof(retries), resp, sizeof(resp)) >= 0;
}

void Pn532Reader::writeFrame(uint8_t cmd, const uint8_t* params, uint8_t len) {
  const uint8_t n = len + 2;  // TFI + command code + params
  uint8_t sum = kHostToPn + cmd;
  begin_();
  delay(2);
  SPI.transfer(kDataWrite);
  SPI.transfer(0x00);  // preamble
  SPI.transfer(0x00);  // start code
  SPI.transfer(0xFF);
  SPI.transfer(n);
  SPI.transfer(static_cast<uint8_t>(~n + 1));
  SPI.transfer(kHostToPn);
  SPI.transfer(cmd);
  for (uint8_t i = 0; i < len; i++) {
    SPI.transfer(params[i]);
    sum += params[i];
  }
  SPI.transfer(static_cast<uint8_t>(~sum + 1));
  SPI.transfer(0x00);  // postamble
  end_();
}

bool Pn532Reader::waitReady(uint32_t timeoutMs) {
  const uint32_t start = millis();
  for (;;) {
    begin_();
    SPI.transfer(kStatusRead);
    const uint8_t st = SPI.transfer(0x00);
    end_();
    if (st == 0x01) return true;
    if (millis() - start >= timeoutMs) return false;
    delay(1);
  }
}

bool Pn532Reader::readAck() {
  static const uint8_t kAck[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
  uint8_t buf[sizeof(kAck)];
  begin_();
  delay(1);
  SPI.transfer(kDataRead);
  for (uint8_t& b : buf) b = SPI.transfer(0x00);
  end_();
  return memcmp(buf, kAck, sizeof(kAck)) == 0;
}

int Pn532Reader::readFrame(uint8_t cmd, uint8_t* resp, uint8_t respMax) {
  begin_();
  delay(1);
  SPI.transfer(kDataRead);
  // Preamble: any 0x00 bytes, then 0x00 0xFF.
  uint8_t b = 0;
  uint8_t skipped = 0;
  do {
    b = SPI.transfer(0x00);
  } while (b == 0x00 && ++skipped < 8);
  const uint8_t n = SPI.transfer(0x00);
  const uint8_t lcs = SPI.transfer(0x00);
  if (b != 0xFF || static_cast<uint8_t>(n + lcs) != 0 || n < 2) {
    end_();
    return -1;  // no frame, or an error frame (length 1)
  }
  const uint8_t tfi = SPI.transfer(0x00);
  const uint8_t code = SPI.transfer(0x00);
  uint8_t sum = tfi + code;
  const uint8_t dataLen = n - 2;
  for (uint8_t i = 0; i < dataLen; i++) {
    const uint8_t d = SPI.transfer(0x00);
    sum += d;
    if (i < respMax) resp[i] = d;
  }
  sum += SPI.transfer(0x00);  // DCS
  SPI.transfer(0x00);         // postamble
  end_();
  if (tfi != kPnToHost || code != cmd + 1 || sum != 0) return -1;
  return min<int>(dataLen, respMax);
}

int Pn532Reader::command(uint8_t cmd, const uint8_t* params, uint8_t len, uint8_t* resp,
                         uint8_t respMax, uint32_t timeoutMs) {
  writeFrame(cmd, params, len);
  if (!waitReady(10) || !readAck()) return -1;
  if (!waitReady(timeoutMs)) return -1;
  return readFrame(cmd, resp, respMax);
}

bool Pn532Reader::exchange(const uint8_t* data, uint8_t len, uint8_t* resp, uint8_t respLen) {
  uint8_t params[1 + 1 + 16 + 10];  // target + MIFARE command + up to 26 bytes
  if (len > sizeof(params) - 1) return false;
  params[0] = 0x01;  // target 1 (the card from InListPassiveTarget)
  memcpy(params + 1, data, len);
  uint8_t buf[1 + 16];
  const int n = command(kCmdInDataExchange, params, len + 1, buf, sizeof(buf));
  if (n < 1 || (buf[0] & 0x3F) != 0) return false;
  if (resp) {
    if (n < 1 + respLen) return false;
    memcpy(resp, buf + 1, respLen);
  }
  return true;
}

// Cycling the RF field returns every card (halted or not) to idle, so a card
// resting on the reader answers each poll, like WakeupA on the RC522.
bool Pn532Reader::select(HfCard& card) {
  field(false);
  field(true);
  delay(3);  // let the card power up
  const uint8_t params[] = {0x01, 0x00};  // one target, 106 kbps type A
  uint8_t resp[6 + sizeof(card.uid)];
  const int n = command(kCmdInListPassiveTarget, params, sizeof(params), resp, sizeof(resp));
  esp_task_wdt_reset();
  // NbTg, Tg, SENS_RES (2), SEL_RES, NFCIDLength, NFCID
  if (n < 6 || resp[0] != 1) return false;
  const uint8_t len = resp[5];
  if (len == 0 || len > sizeof(card.uid) || n < 6 + len) return false;
  card.sak = resp[4];
  card.uidLen = len;
  memcpy(card.uid, resp + 6, len);
  listed_ = true;
  return true;
}

bool Pn532Reader::auth(uint8_t block, bool keyB, const uint8_t key[6], const HfCard& card) {
  if (card.uidLen < 4) return false;
  uint8_t data[2 + 6 + 4];
  data[0] = keyB ? kMifareAuthB : kMifareAuthA;
  data[1] = block;
  memcpy(data + 2, key, 6);
  memcpy(data + 8, card.uid + card.uidLen - 4, 4);  // last 4 UID bytes, as the RC522 does
  return exchange(data, sizeof(data));
}

bool Pn532Reader::read(uint8_t block, uint8_t out[16]) {
  const uint8_t data[] = {kMifareRead, block};
  return exchange(data, sizeof(data), out, 16);
}

bool Pn532Reader::write(uint8_t block, const uint8_t in[16]) {
  uint8_t data[2 + 16];
  data[0] = kMifareWrite;
  data[1] = block;
  memcpy(data + 2, in, 16);
  return exchange(data, sizeof(data));
}

void Pn532Reader::halt() {
  if (!listed_) return;
  const uint8_t params[] = {0x00};  // all targets
  uint8_t resp[1];
  command(kCmdInRelease, params, sizeof(params), resp, sizeof(resp));
  listed_ = false;
}

void Pn532Reader::field(bool on) {
  const uint8_t params[] = {0x01, static_cast<uint8_t>(on ? 0x01 : 0x00)};  // item 1: RF field
  uint8_t resp[1];
  command(kCmdRfConfiguration, params, sizeof(params), resp, sizeof(resp));
}

void Pn532Reader::powerDown() {
  const uint8_t params[] = {0x20};  // wake up on SPI
  uint8_t resp[1];
  command(kCmdPowerDown, params, sizeof(params), resp, sizeof(resp));
}
