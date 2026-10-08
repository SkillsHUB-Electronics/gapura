#include <SPI.h>

#include "config.h"
#include "hf.h"

Rc522Reader::Rc522Reader() : mfrc_(pins::kRfidSs, pins::kRfidRst) {}

bool Rc522Reader::begin() {
  SPI.begin(pins::kRfidSck, pins::kRfidMiso, pins::kRfidMosi, pins::kRfidSs);
  mfrc_.PCD_Init();
  mfrc_.PCD_SetAntennaGain(MFRC522::RxGain_max);  // clone modules (0xB2) need it
  version_ = mfrc_.PCD_ReadRegister(MFRC522::VersionReg);
  // 0x00 / 0xFF: no answer on SPI (wiring or power problem)
  return version_ != 0x00 && version_ != 0xFF;
}

// WakeupA also reaches halted cards, so a card resting on the reader is
// seen on every poll and removal can be detected.
bool Rc522Reader::select(HfCard& card) {
  byte atqa[2];
  byte size = sizeof(atqa);
  const MFRC522::StatusCode st = mfrc_.PICC_WakeupA(atqa, &size);
  if ((st != MFRC522::STATUS_OK && st != MFRC522::STATUS_COLLISION) ||
      !mfrc_.PICC_ReadCardSerial()) {
    return false;
  }
  card.uidLen = min<uint8_t>(mfrc_.uid.size, sizeof(card.uid));
  memcpy(card.uid, mfrc_.uid.uidByte, card.uidLen);
  card.sak = mfrc_.uid.sak;
  return true;
}

bool Rc522Reader::auth(uint8_t block, bool keyB, const uint8_t key[6], const HfCard&) {
  MFRC522::MIFARE_Key k;
  memcpy(k.keyByte, key, sizeof(k.keyByte));
  const auto cmd = keyB ? MFRC522::PICC_CMD_MF_AUTH_KEY_B : MFRC522::PICC_CMD_MF_AUTH_KEY_A;
  return mfrc_.PCD_Authenticate(cmd, block, &k, &mfrc_.uid) == MFRC522::STATUS_OK;
}

bool Rc522Reader::read(uint8_t block, uint8_t out[16]) {
  byte buf[18];  // 16 data + 2 CRC
  byte size = sizeof(buf);
  if (mfrc_.MIFARE_Read(block, buf, &size) != MFRC522::STATUS_OK) return false;
  memcpy(out, buf, 16);
  return true;
}

bool Rc522Reader::write(uint8_t block, const uint8_t in[16]) {
  byte buf[16];
  memcpy(buf, in, sizeof(buf));
  return mfrc_.MIFARE_Write(block, buf, sizeof(buf)) == MFRC522::STATUS_OK;
}

void Rc522Reader::halt() {
  mfrc_.PICC_HaltA();
  mfrc_.PCD_StopCrypto1();
}

void Rc522Reader::field(bool on) {
  if (on) mfrc_.PCD_AntennaOn();
  else mfrc_.PCD_AntennaOff();
}

void Rc522Reader::powerDown() { mfrc_.PCD_SoftPowerDown(); }
