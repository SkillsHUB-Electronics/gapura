// BLE HID keyboard mode: the reader pairs like a Bluetooth keyboard (passkey)
// and types the card text on every tag, so any phone or PC app can receive it.
#pragma once

#include <Arduino.h>

#include <deque>
#include <string>

#include "store.h"

class NimBLECharacteristic;

class BleKeyboard {
 public:
  void begin(Store& store);
  // Sends one queued key event per call at most every kKeyGapMs.
  void loop();

  // Queues `text` (ASCII letters, digits and common punctuation) and Enter.
  void type(const std::string& text, bool enter);

  bool connected() const { return connected_; }
  bool secured() const { return secured_; }

  // Called from NimBLE callbacks (host task).
  void handleConnect() { connected_ = true; }
  void handleDisconnect() { connected_ = false; secured_ = false; }
  void handleSecured(bool ok) { secured_ = ok; }

 private:
  struct Key {
    uint8_t mod;
    uint8_t code;
  };
  static bool toKey(char c, Key& out);
  void sendReport(uint8_t mod, uint8_t code);

  NimBLECharacteristic* input_ = nullptr;
  volatile bool connected_ = false;
  volatile bool secured_ = false;
  std::deque<Key> queue_;
  bool keyDown_ = false;
  uint32_t lastMs_ = 0;
};
