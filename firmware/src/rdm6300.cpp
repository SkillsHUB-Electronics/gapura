#include "rdm6300.h"

#include "config.h"

void Rdm6300::begin() {
  pinMode(pins::kRdmPower, OUTPUT);
  digitalWrite(pins::kRdmPower, LOW);
  Serial1.begin(cfg::kRdmBaud, SERIAL_8N1, pins::kRdmRx, -1);
}

void Rdm6300::power(bool on) {
  if (on == powered_) return;
  powered_ = on;
  digitalWrite(pins::kRdmPower, on ? HIGH : LOW);
  // Bytes from the power edge (or a half frame) are noise.
  while (Serial1.available()) Serial1.read();
  pos_ = 0;
}

bool Rdm6300::poll(uint8_t id[em4100::kIdLen]) {
  bool got = false;
  while (Serial1.available()) {
    const uint8_t b = Serial1.read();
    if (b == 0x02) pos_ = 0;  // start of frame: resync
    if (pos_ == 0 && b != 0x02) continue;
    frame_[pos_++] = b;
    if (pos_ < em4100::kFrameLen) continue;
    pos_ = 0;
    if (em4100::parseFrame(frame_, id)) got = true;
  }
  return got;
}
