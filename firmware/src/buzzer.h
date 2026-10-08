// Active buzzer: non-blocking beeps.
#pragma once

#include <Arduino.h>

class Buzzer {
 public:
  void begin(uint8_t pin);
  void loop();
  void beep(uint32_t ms);

 private:
  uint8_t pin_ = 0;
  uint32_t offAtMs_ = 0;
  bool on_ = false;
};
