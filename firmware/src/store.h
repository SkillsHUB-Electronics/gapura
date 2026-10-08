// Persistent device configuration in NVS (Preferences).
#pragma once

#include <Arduino.h>

#include "util.h"

struct Config {
  String wifiSsid;
  String wifiPass;
  String token;  // API token for HTTP/WS; generated on first boot
  String name;   // "Gapura-XXXX" from the MAC by default
  bool beep = true;
  uint8_t ledBrightness = 60;
  uint8_t batteryLowPct = 15;
  uint16_t batteryCapacityMah = 2000;
  uint32_t blePasskey = 0;  // 6 digits; default on first boot
  // BLE keyboard mode (types the card text on every tag)
  bool kbMode = false;      // false: JSON-over-BLE service
  bool bleOff = false;      // no Bluetooth at all (frees RAM for TLS)
  bool kbUid = false;       // type the UID instead of a block
  bool kbCred = false;      // type the secured credential text
  uint8_t kbBlock = 4;
  String kbKey = "FFFFFFFFFFFF";
  bool kbKeyB = false;
  bool kbEnter = true;
  // Webhook for fixed readers: POST each tag to this URL
  String webhookUrl;    // empty = off
  String webhookToken;  // sent as a Bearer token
  // Card security secret, 32 hex chars (16 bytes); empty = legacy plain cards
  String cardSecret;
  String cardFactoryKey = "FFFFFFFFFFFF";  // key A of blank cards, 12 hex
  bool cardSecEnabled = true;  // false: keep the secret but read plain cards
  reader::Mode readerMode = reader::Mode::kRc522;  // applied after reboot
};

class Store {
 public:
  void begin();  // loads, fills defaults, saves generated values
  void save();

  Config& config() { return cfg_; }

 private:
  Config cfg_;
};
