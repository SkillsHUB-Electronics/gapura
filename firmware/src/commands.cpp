#include "commands.h"

#include "cardsec.h"
#include "config.h"

namespace {

bool gReboot = false;
bool gSleep = false;
constexpr uint32_t kReadUidDefaultMs = 5000;
constexpr uint32_t kReadUidMaxMs = 10000;

// Parses {key, keyType}; defaults to FFFFFFFFFFFF / A.
bool parseKey(JsonObjectConst args, MifareKey& key) {
  const char* hexKey = args["key"] | "FFFFFFFFFFFF";
  const char* type = args["keyType"] | "A";
  if (!hex::decode(hexKey, key.bytes, sizeof(key.bytes))) return false;
  if (strcmp(type, "A") != 0 && strcmp(type, "B") != 0) return false;
  key.typeB = type[0] == 'B';
  return true;
}

bool parseIndex(JsonObjectConst args, const char* name, uint8_t limit,
                uint8_t& out) {
  if (!args[name].is<int>()) return false;
  const int v = args[name];
  if (v < 0 || v >= limit) return false;
  out = static_cast<uint8_t>(v);
  return true;
}

void blockHex(const Block b, JsonVariant out) {
  out.set(hex::encode(b, mifare::kBlockSize));
}

}  // namespace

bool rebootRequested() { return gReboot; }
bool sleepRequested() { return gSleep; }

void batteryToJson(const BatteryInfo& b, JsonObject out) {
  out["v"] = roundf(b.v * 100) / 100;
  out["mA"] = roundf(b.mA * 10) / 10;
  out["mW"] = roundf(b.mW);
  out["pct"] = b.pct;
  out["state"] = toString(b.state);
}

void registerCommands(Router& r, Device& d) {
  r.on("ping", [](JsonObjectConst, JsonObject data) {
    data["fw"] = FW_VERSION;
    return Err::kOk;
  });

  r.on("info", [&d](JsonObjectConst, JsonObject data) {
    data["fw"] = FW_VERSION;
    data["name"] = d.store.config().name;
    data["heap"] = ESP.getFreeHeap();
    data["maxBlock"] = ESP.getMaxAllocHeap();
    data["psramKB"] = ESP.getPsramSize() / 1024;
    data["uptimeS"] = millis() / 1000;
    data["rc522"] = d.rfid.hfReady();  // 13.56 MHz chip answering (RC522 or PN532)
    data["ina219"] = d.power.sensorOk();
    data["scanning"] = d.rfid.scanning();
    data["ip"] = d.net.ip();
    data["rssi"] = d.net.rssi();
    data["webhook"] = d.webhook.enabled() ? d.webhook.lastStatus() : 0;
    data["defaultPassword"] = d.store.config().token == cfg::kDefaultPassword;
    JsonObject rd = data["reader"].to<JsonObject>();
    rd["mode"] = reader::toString(d.rfid.mode());
    if (d.rfid.hfChip()) {
      rd["hf"] = d.rfid.hfChip();
      rd["hfOk"] = d.rfid.hfReady();
    } else {
      rd["hf"] = nullptr;
      rd["hfOk"] = false;
    }
    rd["lf"] = d.rfid.hasLf() ? "RDM6300" : nullptr;
    JsonObject links = data["links"].to<JsonObject>();
    links["usb"] = true;
    links["wifi"] = d.net.connected();
    links["wsClients"] = d.net.clients();
    links["ble"] = d.ble.secured();
    batteryToJson(d.power.info(), data["battery"].to<JsonObject>());
    return Err::kOk;
  });

  r.on("battery", [&d](JsonObjectConst, JsonObject data) {
    batteryToJson(d.power.info(), data);
    return Err::kOk;
  });

  r.on("scan_start", [&d](JsonObjectConst, JsonObject) {
    d.rfid.setScanning(true);
    return Err::kOk;
  });
  r.on("scan_stop", [&d](JsonObjectConst, JsonObject) {
    d.rfid.setScanning(false);
    return Err::kOk;
  });

  r.on("read_uid", [&d](JsonObjectConst args, JsonObject data) {
    const uint32_t timeout =
        min<uint32_t>(args["timeoutMs"] | kReadUidDefaultMs, kReadUidMaxMs);
    TagInfo tag;
    if (!d.rfid.waitUid(timeout, tag)) return Err::kNoCard;
    data["uid"] = tag.uid;
    data["type"] = tag.type;
    return Err::kOk;
  });

  r.on("read_block", [&d](JsonObjectConst args, JsonObject data) {
    uint8_t block;
    MifareKey key;
    if (!parseIndex(args, "block", mifare::kBlockCount, block) ||
        !parseKey(args, key)) {
      return Err::kBadArgs;
    }
    Block buf;
    const Err err = d.rfid.readBlock(block, key, buf);
    if (err != Err::kOk) return err;
    data["block"] = block;
    blockHex(buf, data["hex"].to<JsonVariant>());
    return Err::kOk;
  });

  r.on("write_block", [&d](JsonObjectConst args, JsonObject data) {
    uint8_t block;
    MifareKey key;
    Block buf;
    if (!parseIndex(args, "block", mifare::kBlockCount, block) ||
        !parseKey(args, key) ||
        !hex::decode(args["hex"] | "", buf, sizeof(buf))) {
      return Err::kBadArgs;
    }
    if (d.power.info().critical) return Err::kLowBattery;
    const Err err = d.rfid.writeBlock(block, key, buf);
    if (err != Err::kOk) return err;
    d.led.flash(Led::kWriteOk);
    data["block"] = block;
    return Err::kOk;
  });

  r.on("read_sector", [&d](JsonObjectConst args, JsonObject data) {
    uint8_t sector;
    MifareKey key;
    if (!parseIndex(args, "sector", mifare::kSectorCount, sector) ||
        !parseKey(args, key)) {
      return Err::kBadArgs;
    }
    Block blocks[4];
    const Err err = d.rfid.readSector(sector, key, blocks);
    if (err != Err::kOk) return err;
    data["sector"] = sector;
    JsonArray arr = data["blocks"].to<JsonArray>();
    for (auto& b : blocks) blockHex(b, arr.add<JsonVariant>());
    return Err::kOk;
  });

  // Sectors that fail keep going; each carries its own error code.
  r.on("dump", [&d](JsonObjectConst args, JsonObject data) {
    MifareKey key;
    if (!parseKey(args, key)) return Err::kBadArgs;
    JsonArray sectors = data["sectors"].to<JsonArray>();
    uint8_t okCount = 0;
    for (uint8_t s = 0; s < mifare::kSectorCount; s++) {
      Block blocks[4];
      const Err err = d.rfid.readSector(s, key, blocks);
      if (err == Err::kNoCard || err == Err::kUnsupportedCard) return err;
      JsonObject item = sectors.add<JsonObject>();
      item["sector"] = s;
      if (err != Err::kOk) {
        item["error"] = errCode(err);
        continue;
      }
      okCount++;
      JsonArray arr = item["blocks"].to<JsonArray>();
      for (auto& b : blocks) blockHex(b, arr.add<JsonVariant>());
    }
    return okCount ? Err::kOk : Err::kAuthFailed;
  });

  // Writes UTF-8 text across data blocks, skipping trailers, zero-padded.
  r.on("write_text", [&d](JsonObjectConst args, JsonObject data) {
    uint8_t block;
    MifareKey key;
    const char* text = args["text"];
    if (!parseIndex(args, "startBlock", mifare::kBlockCount, block) ||
        !parseKey(args, key) || !text) {
      return Err::kBadArgs;
    }
    if (!mifare::isWritable(block)) return Err::kForbiddenBlock;
    if (d.power.info().critical) return Err::kLowBattery;

    const size_t len = strlen(text);
    size_t pos = 0;
    JsonArray written = data["blocks"].to<JsonArray>();
    do {
      while (block < mifare::kBlockCount && !mifare::isWritable(block)) block++;
      if (block >= mifare::kBlockCount) return Err::kBadArgs;  // too long
      Block buf = {0};
      const size_t n = min<size_t>(mifare::kBlockSize, len - pos);
      memcpy(buf, text + pos, n);
      const Err err = d.rfid.writeBlock(block, key, buf);
      if (err != Err::kOk) return err;
      written.add(block);
      pos += n;
      block++;
    } while (pos < len);
    d.led.flash(Led::kWriteOk);
    return Err::kOk;
  });

  // Card security needs the shared secret (config `security.secret`).
  // Credential: user text (up to 95 chars), locked per card and signed with the secret.
  r.on("card_write_credential", [&d](JsonObjectConst args, JsonObject data) {
    uint8_t secret[cardsec::kSecretLen];
    if (!cardsec::parseSecret(d.store.config().cardSecret.c_str(), secret)) return Err::kBadArgs;
    if (!args["text"].is<const char*>()) return Err::kBadArgs;
    if (d.power.info().critical) return Err::kLowBattery;
    const char* text = args["text"];
    uint8_t factory[6];
    const char* fk = args["factoryKey"] | d.store.config().cardFactoryKey.c_str();
    if (!hex::decode(fk, factory, sizeof(factory))) return Err::kBadArgs;
    String uid;
    bool keyed = false;
    const Err err = d.rfid.writeCredential(secret, factory, text, &uid, &keyed);
    if (err != Err::kOk) return err;
    d.led.flash(Led::kWriteOk);
    data["uid"] = uid;
    data["text"] = text;
    data["keyed"] = keyed;
    return Err::kOk;
  });

  r.on("card_read_credential", [&d](JsonObjectConst, JsonObject data) {
    uint8_t secret[cardsec::kSecretLen];
    if (!cardsec::parseSecret(d.store.config().cardSecret.c_str(), secret)) return Err::kBadArgs;
    std::string text;
    String uid;
    const Err err = d.rfid.readCredential(secret, text, &uid);
    if (err != Err::kOk) return err;
    data["uid"] = uid;
    data["text"] = text;
    return Err::kOk;
  });

  // `secret` (optional) is the secret that locked the card, when it is not this reader's.
  r.on("card_reset", [&d](JsonObjectConst args, JsonObject data) {
    uint8_t secret[cardsec::kSecretLen];
    const char* oldSecret = args["secret"] | d.store.config().cardSecret.c_str();
    if (!cardsec::parseSecret(oldSecret, secret)) return Err::kBadArgs;
    if (d.power.info().critical) return Err::kLowBattery;
    uint8_t factory[6];
    const char* fk = args["factoryKey"] | d.store.config().cardFactoryKey.c_str();
    if (!hex::decode(fk, factory, sizeof(factory))) return Err::kBadArgs;
    String uid;
    const Err err = d.rfid.resetCard(secret, factory, &uid);
    if (err != Err::kOk) return err;
    d.led.flash(Led::kWriteOk);
    data["uid"] = uid;
    return Err::kOk;
  });

  // Sends one sample POST so the URL, token and TLS can be checked without a
  // card; the result arrives as the `webhook` event.
  r.on("webhook_test", [&d](JsonObjectConst, JsonObject) {
    if (!d.webhook.enabled()) return Err::kBadArgs;
    JsonDocument body;
    body["device"] = d.store.config().name;
    body["uid"] = "00000000";
    body["reader"] = "test";
    body["text"] = nullptr;
    body["verified"] = nullptr;
    body["test"] = true;
    body["ts"] = millis();
    std::string json;
    serializeJson(body, json);
    d.webhook.post(json);
    return Err::kOk;
  });

  // Hardware tests for the dashboard: LED (colour or status look) and buzzer.
  r.on("led_test", [&d](JsonObjectConst args, JsonObject) {
    const uint32_t ms = constrain(args["ms"] | 3000, 200, 10000);
    if (args["status"].is<const char*>()) {
      return d.led.testStatus(args["status"], ms) ? Err::kOk : Err::kBadArgs;
    }
    if (args["r"].isNull() || args["g"].isNull() || args["b"].isNull()) return Err::kBadArgs;
    const int rgb[3] = {args["r"] | -1, args["g"] | -1, args["b"] | -1};
    for (int v : rgb) {
      if (v < 0 || v > 255) return Err::kBadArgs;
    }
    d.led.testColor(rgb[0], rgb[1], rgb[2], ms);
    return Err::kOk;
  });
  r.on("buzzer_test", [&d](JsonObjectConst args, JsonObject) {
    d.buzzer.beep(constrain(args["ms"] | 300, 20, 2000));
    return Err::kOk;
  });

  // Wi-Fi setup over any link. wifi_scan is polled until `scanning` is false.
  r.on("wifi_scan", [&d](JsonObjectConst, JsonObject data) {
    d.net.wifiScan(data);
    return Err::kOk;
  });
  r.on("wifi_connect", [&d](JsonObjectConst args, JsonObject data) {
    if (!args["ssid"].is<const char*>()) return Err::kBadArgs;
    const String ssid = args["ssid"].as<const char*>();
    const String pass = args["pass"] | "";
    if (!d.net.wifiConnect(ssid, pass)) return Err::kBadArgs;  // bad length, or busy
    d.net.wifiStatus(data);
    return Err::kOk;
  });
  r.on("wifi_status", [&d](JsonObjectConst, JsonObject data) {
    d.net.wifiStatus(data);
    return Err::kOk;
  });

  r.on("config_get", [&d](JsonObjectConst, JsonObject data) {
    const Config& c = d.store.config();
    data["wifi"]["ssid"] = c.wifiSsid;
    data["wifi"]["pass"] = c.wifiPass.isEmpty() ? "" : "********";
    data["token"] = "********";
    data["name"] = c.name;
    data["beep"] = c.beep;
    data["led"]["brightness"] = c.ledBrightness;
    data["battery"]["lowPct"] = c.batteryLowPct;
    data["battery"]["capacityMah"] = c.batteryCapacityMah;
    data["ble"]["passkey"] = c.blePasskey;  // needed to pair; links are authorised
    data["mode"] = c.bleOff ? "off" : c.kbMode ? "keyboard" : "app";
    data["reader"]["mode"] = reader::toString(c.readerMode);
    data["keyboard"]["source"] = c.kbCred ? "credential" : c.kbUid ? "uid" : "block";
    data["keyboard"]["block"] = c.kbBlock;
    data["keyboard"]["key"] = "************";
    data["keyboard"]["keyType"] = c.kbKeyB ? "B" : "A";
    data["keyboard"]["enter"] = c.kbEnter;
    data["webhook"]["url"] = c.webhookUrl;
    uint8_t sec[cardsec::kSecretLen];
    const bool secSet = cardsec::parseSecret(c.cardSecret.c_str(), sec);
    data["security"]["set"] = secSet;
    data["security"]["enabled"] = c.cardSecEnabled;
    data["security"]["id"] = secSet ? cardsec::fingerprint(sec) : String("");
    data["security"]["factoryKey"] = "************";
    data["webhook"]["token"] = c.webhookToken.isEmpty() ? "" : "********";
    return Err::kOk;
  });

  // Accepts any subset of the config_get shape; validates before saving.
  r.on("config_set", [&d](JsonObjectConst args, JsonObject) {
    if (args.isNull()) return Err::kBadArgs;
    Config next = d.store.config();
    if (args["wifi"]["ssid"].is<const char*>()) next.wifiSsid = args["wifi"]["ssid"].as<const char*>();
    if (args["wifi"]["pass"].is<const char*>()) next.wifiPass = args["wifi"]["pass"].as<const char*>();
    if (args["token"].is<const char*>()) {
      next.token = args["token"].as<const char*>();
      if (next.token.length() < 8) return Err::kBadArgs;
    }
    if (args["name"].is<const char*>()) {
      next.name = args["name"].as<const char*>();
      if (next.name.isEmpty() || next.name.length() > 24) return Err::kBadArgs;
    }
    if (args["beep"].is<bool>()) next.beep = args["beep"];
    if (!args["led"]["brightness"].isNull()) {
      const int v = args["led"]["brightness"] | -1;
      if (v < 0 || v > 100) return Err::kBadArgs;
      next.ledBrightness = v;
    }
    if (!args["battery"]["lowPct"].isNull()) {
      const int v = args["battery"]["lowPct"] | -1;
      if (v < 5 || v > 50) return Err::kBadArgs;
      next.batteryLowPct = v;
    }
    if (!args["battery"]["capacityMah"].isNull()) {
      const int v = args["battery"]["capacityMah"] | -1;
      if (v < 100 || v > 20000) return Err::kBadArgs;
      next.batteryCapacityMah = v;
    }
    if (!args["ble"]["passkey"].isNull()) {
      const long v = args["ble"]["passkey"] | -1L;
      if (v < 100000 || v > 999999) return Err::kBadArgs;
      next.blePasskey = v;  // applied after reboot
    }
    if (args["mode"].is<const char*>()) {
      const String m = args["mode"].as<const char*>();
      if (m != "app" && m != "keyboard" && m != "off") return Err::kBadArgs;
      next.kbMode = m == "keyboard";  // applied after reboot
      next.bleOff = m == "off";
    }
    if (!args["reader"]["mode"].isNull()) {
      if (!reader::parse(args["reader"]["mode"] | "", next.readerMode)) return Err::kBadArgs;
      // applied after reboot
    }
    JsonObjectConst kb = args["keyboard"];
    if (kb["source"].is<const char*>()) {
      const String s = kb["source"].as<const char*>();
      if (s != "block" && s != "uid" && s != "credential") return Err::kBadArgs;
      next.kbUid = s == "uid";
      next.kbCred = s == "credential";
    }
    if (!kb["block"].isNull()) {
      const int v = kb["block"] | -1;
      if (v < 1 || v >= mifare::kBlockCount || mifare::isTrailer(v)) return Err::kBadArgs;
      next.kbBlock = v;
    }
    if (kb["key"].is<const char*>()) {
      uint8_t probe[6];
      if (!hex::decode(kb["key"].as<const char*>(), probe, sizeof(probe))) return Err::kBadArgs;
      next.kbKey = kb["key"].as<const char*>();
    }
    if (kb["keyType"].is<const char*>()) {
      const String t = kb["keyType"].as<const char*>();
      if (t != "A" && t != "B") return Err::kBadArgs;
      next.kbKeyB = t == "B";
    }
    if (kb["enter"].is<bool>()) next.kbEnter = kb["enter"];
    if (args["security"]["secret"].is<const char*>()) {
      const String s = args["security"]["secret"].as<const char*>();
      uint8_t probe[cardsec::kSecretLen];
      if (!s.isEmpty() && !cardsec::parseSecret(s.c_str(), probe)) return Err::kBadArgs;
      next.cardSecret = s;  // empty turns card security off
    }
    if (args["security"]["enabled"].is<bool>()) next.cardSecEnabled = args["security"]["enabled"];
    if (args["security"]["factoryKey"].is<const char*>()) {
      const String f = args["security"]["factoryKey"].as<const char*>();
      uint8_t probe[6];
      if (!hex::decode(f.c_str(), probe, sizeof(probe))) return Err::kBadArgs;
      next.cardFactoryKey = f;
    }
    if (args["webhook"]["url"].is<const char*>()) {
      const String u = args["webhook"]["url"].as<const char*>();
      if (!u.isEmpty() && !u.startsWith("http://") && !u.startsWith("https://")) return Err::kBadArgs;
      if (u.length() > 200) return Err::kBadArgs;
      next.webhookUrl = u;
    }
    if (args["webhook"]["token"].is<const char*>()) {
      next.webhookToken = args["webhook"]["token"].as<const char*>();
      if (next.webhookToken.length() > 128) return Err::kBadArgs;
    }
    d.store.config() = next;
    d.store.save();
    d.led.setBrightness(next.ledBrightness);
    d.power.setLowPct(next.batteryLowPct);
    d.power.setCapacityMah(next.batteryCapacityMah);
    return Err::kOk;
  });

  r.on("sleep", [](JsonObjectConst, JsonObject) {
    gSleep = true;  // only the RESET (EN) button wakes the chip
    return Err::kOk;
  });

  r.on("reboot", [](JsonObjectConst, JsonObject) {
    gReboot = true;
    return Err::kOk;
  });
}
