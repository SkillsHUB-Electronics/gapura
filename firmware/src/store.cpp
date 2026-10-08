#include "store.h"

#include <Preferences.h>
#include <esp_random.h>

#include "config.h"

namespace {
constexpr const char* kNamespace = "rfid";
}

void Store::begin() {
  Preferences p;
  p.begin(kNamespace, true);
  cfg_.wifiSsid = p.getString("ssid", "");
  cfg_.wifiPass = p.getString("pass", "");
  cfg_.token = p.getString("token", "");
  cfg_.name = p.getString("name", "");
  cfg_.beep = p.getBool("beep", true);
  cfg_.ledBrightness = p.getUChar("ledBri", cfg::kLedDefaultBrightness);
  cfg_.batteryLowPct = p.getUChar("lowPct", 15);
  cfg_.batteryCapacityMah = p.getUShort("capMah", 2000);
  cfg_.blePasskey = p.getULong("blePin", 0);
  cfg_.kbMode = p.getBool("kbMode", false);
  cfg_.bleOff = p.getBool("bleOff", false);
  cfg_.kbUid = p.getBool("kbUid", false);
  cfg_.kbCred = p.getBool("kbCred", false);
  cfg_.kbBlock = p.getUChar("kbBlock", 4);
  cfg_.kbKey = p.getString("kbKey", "FFFFFFFFFFFF");
  cfg_.kbKeyB = p.getBool("kbKeyB", false);
  cfg_.kbEnter = p.getBool("kbEnter", true);
  cfg_.webhookUrl = p.getString("whUrl", "");
  cfg_.webhookToken = p.getString("whTok", "");
  cfg_.cardSecret = p.getString("cardSec", "");
  cfg_.cardFactoryKey = p.getString("cardFk", "FFFFFFFFFFFF");
  cfg_.cardSecEnabled = p.getBool("cardOn", true);
  cfg_.readerMode = reader::fromIndex(p.getUChar("rdrMode", 0));
  p.end();

  bool dirty = false;
  if (cfg_.token.isEmpty()) {
    cfg_.token = cfg::kDefaultPassword;
    dirty = true;
  }
  if (cfg_.blePasskey < 100000 || cfg_.blePasskey > 999999) {
    cfg_.blePasskey = cfg::kDefaultBlePasskey;
    dirty = true;
  }
  if (cfg_.name.startsWith("RFID-")) {  // pre-Gapura default name: keep the 4-hex suffix
    cfg_.name = "Gapura-" + cfg_.name.substring(5);
    dirty = true;
  }
  if (cfg_.name.isEmpty()) {
    char buf[12];
    snprintf(buf, sizeof(buf), "Gapura-%04X",
             static_cast<unsigned>(ESP.getEfuseMac() >> 32) & 0xFFFF);
    cfg_.name = buf;
    dirty = true;
  }
  if (dirty) save();
}

void Store::save() {
  Preferences p;
  p.begin(kNamespace, false);
  p.putString("ssid", cfg_.wifiSsid);
  p.putString("pass", cfg_.wifiPass);
  p.putString("token", cfg_.token);
  p.putString("name", cfg_.name);
  p.putBool("beep", cfg_.beep);
  p.putUChar("ledBri", cfg_.ledBrightness);
  p.putUChar("lowPct", cfg_.batteryLowPct);
  p.putUShort("capMah", cfg_.batteryCapacityMah);
  p.putULong("blePin", cfg_.blePasskey);
  p.putBool("kbMode", cfg_.kbMode);
  p.putBool("bleOff", cfg_.bleOff);
  p.putBool("kbUid", cfg_.kbUid);
  p.putBool("kbCred", cfg_.kbCred);
  p.putUChar("kbBlock", cfg_.kbBlock);
  p.putString("kbKey", cfg_.kbKey);
  p.putBool("kbKeyB", cfg_.kbKeyB);
  p.putBool("kbEnter", cfg_.kbEnter);
  p.putString("whUrl", cfg_.webhookUrl);
  p.putString("whTok", cfg_.webhookToken);
  p.putString("cardSec", cfg_.cardSecret);
  p.putString("cardFk", cfg_.cardFactoryKey);
  p.putBool("cardOn", cfg_.cardSecEnabled);
  p.putUChar("rdrMode", static_cast<uint8_t>(cfg_.readerMode));
  p.end();
}
