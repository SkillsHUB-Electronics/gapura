#include "buzzer.h"

void Buzzer::begin(uint8_t pin) {
  pin_ = pin;
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, LOW);
}

void Buzzer::beep(uint32_t ms) {
  digitalWrite(pin_, HIGH);
  on_ = true;
  offAtMs_ = millis() + ms;
}

void Buzzer::loop() {
  if (on_ && static_cast<int32_t>(millis() - offAtMs_) >= 0) {
    digitalWrite(pin_, LOW);
    on_ = false;
  }
}
