// RDM6300 125 kHz reader (EM4100 tags, UID only) on UART1, receive only.
// While a tag is in the field the module repeats its frame; power is switched
// through pins::kRdmPower so it can share time slots with the 13.56 MHz reader.
#pragma once

#include <Arduino.h>

#include "util.h"

class Rdm6300 {
 public:
  void begin();
  void power(bool on);
  bool powered() const { return powered_; }
  // Reads the UART; true when a valid frame arrived (tag id in `id`).
  bool poll(uint8_t id[em4100::kIdLen]);

 private:
  uint8_t frame_[em4100::kFrameLen];
  uint8_t pos_ = 0;
  bool powered_ = false;
};
