// Wires the modules together. Data flow: docs/PLAN.md §2.2.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>

#include "ble.h"
#include "buzzer.h"

#include "cardsec.h"
#include "commands.h"
#include "keyboard.h"
#include "config.h"
#include "led.h"
#include "net.h"
#include "power.h"
#include "rfid.h"
#include "router.h"
#include "serial_link.h"
#include "store.h"
#include "webhook.h"

namespace {

Rfid rfid;
Power power;
Led led;
Store store;
Router router;
SerialLink serialLink;
Net net;
Ble ble;
BleKeyboard keyboard;
bool tagPending = false;
TagInfo kbTag;
Webhook webhook;
Buzzer buzzer;
Device device{rfid, power, store, led, net, ble, webhook, buzzer};

uint32_t lastBatteryEventMs = 0;
uint32_t lastRfidErrorMs = 0;
uint32_t criticalSinceMs = 0;

void publishTag(const char* event, const TagInfo& tag, bool full) {
  JsonDocument data;
  data["uid"] = tag.uid;
  if (full) {
    data["type"] = tag.type;
    data["reader"] = tag.reader;
    data["ts"] = millis();
  }
  router.publish(event, data);
}

void publishBattery(const BatteryInfo& b) {
  JsonDocument data;
  batteryToJson(b, data.to<JsonObject>());
  router.publish("battery", data);
  lastBatteryEventMs = millis();
}

// Text from the plain block in the keyboard settings (card security off).
bool readPlainText(std::string& text) {
  const Config& c = store.config();
  MifareKey key{};
  key.typeB = c.kbKeyB;
  Block buf;
  if (!hex::decode(c.kbKey.c_str(), key.bytes, sizeof(key.bytes)) ||
      rfid.readBlock(c.kbBlock, key, buf) != Err::kOk) {
    return false;
  }
  text.clear();
  for (size_t i = 0; i < sizeof(buf) && buf[i]; i++) {
    if (buf[i] >= 0x20 && buf[i] < 0x7F) text += static_cast<char>(buf[i]);
  }
  while (!text.empty() && text.back() == ' ') text.pop_back();
  return !text.empty();
}

// Credential of a secured card. A card moved or half in the field fails a read or
// the key check at random: retry a few times, and give up at once only on a real
// signature mismatch.
bool readCredentialText(std::string& text) {
  const Config& c = store.config();
  uint8_t secret[cardsec::kSecretLen];
  if (!c.cardSecEnabled || !cardsec::parseSecret(c.cardSecret.c_str(), secret)) return false;
  for (uint8_t attempt = 0; attempt < 3; attempt++) {
    const Err e = rfid.readCredential(secret, text);
    if (e == Err::kOk && !text.empty()) return true;
    if (e == Err::kBadSignature) break;
    delay(10);
  }
  return false;
}

// Runs once the tag callback has returned, so the SPI read does not happen
// inside the poll. With card security on, the credential (signed) is used;
// otherwise the plain block. Keyboard mode types it, the webhook posts it.
void deliverTag() {
  if (!tagPending) return;
  tagPending = false;
  const Config& c = store.config();
  std::string uid;
  for (size_t i = 0; i < kbTag.uid.length(); i++) {
    if (kbTag.uid[i] != ' ') uid += kbTag.uid[i];
  }
  uint8_t secret[cardsec::kSecretLen];
  const bool secured = c.cardSecEnabled && cardsec::parseSecret(c.cardSecret.c_str(), secret);
  std::string credential, text;
  bool haveCred = false, haveText = false;
  if (kbTag.lf) {
    // 125 kHz tag: UID only. No credential, so with security on it is never verified.
  } else if (secured) {
    haveCred = (webhook.enabled() || c.kbMode) && readCredentialText(credential);
  } else if (webhook.enabled() || c.kbMode) {
    haveText = readPlainText(text);
  }

  if (c.kbMode && kbTag.lf) {
    // 125 kHz tag: its UID, and only while card security is off.
    if (secured) led.flash(Led::kError);
    else keyboard.type(uid, c.kbEnter);
  } else if (c.kbMode) {
    // Security on: only a card with a valid credential types (its credential, or its UID).
    const std::string& out = c.kbUid ? uid : (secured ? credential : text);
    const bool allowed = c.kbUid ? true : (secured ? haveCred : haveText);
    if (out.empty() || !allowed || (secured && !haveCred)) led.flash(Led::kError);
    else keyboard.type(out, c.kbEnter);
  }
  if (webhook.enabled()) {
    JsonDocument body;
    body["device"] = c.name;
    body["uid"] = uid;
    body["reader"] = kbTag.reader;
    if (secured) {
      body["credential"] = haveCred ? credential.c_str() : nullptr;
      body["verified"] = haveCred;  // a valid signature under this reader's secret
      body["text"] = nullptr;
    } else {
      body["credential"] = nullptr;
      body["verified"] = nullptr;
      if (haveText) body["text"] = text;
      else body["text"] = nullptr;
    }
    body["ts"] = millis();
    std::string json;
    serializeJson(body, json);
    webhook.post(json);
  }
}

void beep() {
  if (!store.config().beep) return;
  buzzer.beep(40);
}

// Announces, powers the peripherals down and sleeps. `wakeS` 0: no timer, only
// the RESET (EN) button wakes the chip.
void deepSleep(const char* reason, uint32_t wakeS, int pct = -1) {
  JsonDocument data;
  data["reason"] = reason;
  if (pct >= 0) data["pct"] = pct;
  router.publish("sleep", data);
  delay(300);  // let the event leave
  rfid.powerDown();
  led.off();
  if (wakeS) esp_sleep_enable_timer_wakeup(uint64_t{wakeS} * 1000000ULL);
  esp_deep_sleep_start();
}

// Critical battery for kCriticalSleepMs while not charging: announce, then
// deep-sleep and wake on a timer to re-check (no charger pin to wake on).
void checkCriticalSleep() {
  const BatteryInfo& b = power.info();
  if (!b.critical) {
    criticalSinceMs = 0;
    return;
  }
  if (!criticalSinceMs) criticalSinceMs = millis() | 1;
  if (millis() - criticalSinceMs < cfg::kCriticalSleepMs) return;

  deepSleep("battery_critical", cfg::kSleepWakeS, b.pct);
}

// BOOT button. On this board the USB chip's DTR line also pulls BOOT low, so a
// serial program holding DTR looks like a long press. Every action therefore
// needs short presses, which a held line cannot make:
// - double press: deep sleep (wake with RESET);
// - hold 10 s (beep), release (beep), then one short press within 5 s: forget
//   the Wi-Fi network and restart in setup mode.
// A press that was already down at boot is ignored until the pin goes high.
// Edges are timestamped in an interrupt: the loop can be busy for a few hundred
// ms (PN532 polls, webhook), longer than a quick press.
constexpr uint8_t kBtnEdges = 16;  // power of two
volatile uint32_t btnEdgeMs[kBtnEdges];
volatile bool btnEdgeDown[kBtnEdges];
volatile uint8_t btnHead = 0;
uint8_t btnTail = 0;

void IRAM_ATTR onButtonEdge() {
  const uint8_t i = btnHead;
  btnEdgeMs[i] = millis();
  btnEdgeDown[i] = digitalRead(pins::kBootButton) == LOW;
  btnHead = (i + 1) & (kBtnEdges - 1);
}

uint32_t btnDownMs = 0;       // start of the current press, 0 = released
uint32_t btnLastShortMs = 0;  // release time of the last short press
bool btnSeenHigh = false;     // the pin was high at least once since boot
bool btnLongArmed = false;    // held 10 s: waiting for release, then the confirming press
uint32_t btnConfirmUntil = 0;

void forgetWifi() {
  Config& c = store.config();
  c.wifiSsid = "";
  c.wifiPass = "";
  store.save();
  for (int i = 0; i < 3; i++) {  // three beeps: Wi-Fi forgotten
    buzzer.beep(120);
    delay(250);
    buzzer.loop();
  }
  ESP.restart();
}

// One debounced edge: a press starts, or a release ends a press of `held` ms.
void buttonEdge(bool down, uint32_t t) {
  if (down) {
    if (!btnDownMs) btnDownMs = t | 1;
    return;
  }
  if (!btnDownMs) return;
  const uint32_t held = t - btnDownMs;
  btnDownMs = 0;
  if (btnLongArmed) {
    btnLongArmed = false;
    btnConfirmUntil = (t + cfg::kButtonConfirmMs) | 1;
    buzzer.beep(80);
    return;
  }
  if (held < cfg::kButtonDebounceMs) return;  // bounce: ignore, keep any pending first press
  if (held > cfg::kButtonShortMs) {
    btnLastShortMs = 0;
    return;
  }
  if (btnConfirmUntil && static_cast<int32_t>(btnConfirmUntil - t) >= 0) forgetWifi();
  btnConfirmUntil = 0;
  if (btnLastShortMs && t - btnLastShortMs <= cfg::kButtonDoubleMs) {
    btnLastShortMs = 0;
    buzzer.beep(80);
    led.flash(Led::kTagOk);
    deepSleep("button", 0);
  }
  btnLastShortMs = t | 1;
}

void checkButton() {
  const uint32_t now = millis();
  if (!btnSeenHigh) {
    btnTail = btnHead;  // drop edges from a press held since boot
    btnSeenHigh = digitalRead(pins::kBootButton) == HIGH;
    return;
  }
  while (btnTail != btnHead) {
    buttonEdge(btnEdgeDown[btnTail], btnEdgeMs[btnTail]);
    btnTail = (btnTail + 1) & (kBtnEdges - 1);
  }
  if (btnConfirmUntil && static_cast<int32_t>(btnConfirmUntil - now) < 0) btnConfirmUntil = 0;
  // Signed: btnDownMs is an interrupt timestamp (| 1), so it can be 1 ms ahead of now.
  if (btnDownMs && !btnLongArmed &&
      static_cast<int32_t>(now - btnDownMs) >= static_cast<int32_t>(cfg::kButtonWifiResetMs)) {
    btnLongArmed = true;
    buzzer.beep(80);  // release now, then press once to confirm
    led.flash(Led::kError);
  }
}

// The core starts the task watchdog at 5 s without reset. Reconfigure it with
// a longer timeout that resets the chip, and watch the loop task too.
void startWatchdog() {
  const esp_task_wdt_config_t wdt = {
      .timeout_ms = cfg::kWatchdogS * 1000,
      .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
      .trigger_panic = true,
  };
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
  esp_task_wdt_add(nullptr);
}

}  // namespace

void setup() {
  led.begin();  // first: drives the common-anode pins HIGH (off)
  Serial.begin(cfg::kSerialBaud);
  buzzer.begin(pins::kBuzzer);
  pinMode(pins::kBootButton, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pins::kBootButton), onButtonEdge, CHANGE);

  store.begin();
  const Config& c = store.config();
  led.setBrightness(c.ledBrightness);
  power.setLowPct(c.batteryLowPct);
  power.setCapacityMah(c.batteryCapacityMah);

  const bool rfidOk = rfid.begin(c.readerMode);
  const bool inaOk = power.begin();

  serialLink.begin(router);
  net.begin(router, store, led);
  if (c.bleOff) {
    // no Bluetooth: leaves about 20 KB more RAM, enough for a TLS handshake
  } else if (c.kbMode) {
    keyboard.begin(store);
  } else {
    ble.begin(router, store);
  }
  webhook.begin(store);
  registerCommands(router, device);
  router.onResult([](Err e) {
    if (e != Err::kOk) led.flash(Led::kError);
  });

  rfid.onTag([](const TagInfo& t) {
    led.flash(Led::kTagOk);
    beep();
    publishTag("tag", t, true);
    if (store.config().kbMode || webhook.enabled()) {
      kbTag = t;
      tagPending = true;
    }
  });
  rfid.onRemoved([](const TagInfo& t) { publishTag("tag_removed", t, false); });

  power.onChange([](const BatteryInfo& b) {
    led.set(Led::kCharging, b.state == ChargeState::kCharging);
    led.set(Led::kLowBattery, b.low);
    led.set(Led::kCritical, b.critical);
    publishBattery(b);
  });
  power.onSample([](const BatteryInfo& b) {
    if (millis() - lastBatteryEventMs >= cfg::kBatteryEventMs) publishBattery(b);
  });

  char ver[5];
  snprintf(ver, sizeof(ver), "0x%02X", rfid.version());
  JsonDocument boot;
  boot["fw"] = FW_VERSION;
  boot["name"] = c.name;
  boot["rc522"] = ver;  // 13.56 MHz chip version (RC522 VersionReg or PN532 firmware)
  boot["reader"] = reader::toString(rfid.mode());
  boot["ina219"] = inaOk;
  boot["ok"] = rfidOk;  // the INA219 is optional (battery voltage comes from the ADC)
  router.publish("boot", boot);

  startWatchdog();
}

void loop() {
  serialLink.loop();
  net.loop();
  ble.loop();
  keyboard.loop();
  buzzer.loop();
  led.set(Led::kClientConnected, net.clients() > 0 || ble.connected() || keyboard.connected());
  rfid.loop();
  deliverTag();
  Webhook::Result wr;
  while (webhook.takeResult(wr)) {
    JsonDocument data;
    data["status"] = wr.status;
    data["ms"] = wr.ms;
    data["body"] = wr.body;
    router.publish("webhook", data);
  }
  power.loop();
  led.loop();

  // 13.56 MHz chip missing: repeat the error flash so it is noticed.
  if (!rfid.ready() && millis() - lastRfidErrorMs > 3000) {
    lastRfidErrorMs = millis();
    led.flash(Led::kError);
  }

  checkCriticalSleep();
  checkButton();
  esp_task_wdt_reset();

  if (sleepRequested()) {
    Serial.flush();
    delay(300);  // let the HTTP/WS response leave
    deepSleep("command", 0);
  }

  if (rebootRequested() || net.rebootRequested()) {
    Serial.flush();
    delay(500);  // let the HTTP/WS response leave
    ESP.restart();
  }
}
