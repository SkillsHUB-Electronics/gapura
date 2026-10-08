#include "led.h"

#include "config.h"

namespace {

constexpr uint8_t kPins[3] = {pins::kLedR, pins::kLedG, pins::kLedB};
constexpr uint32_t kFlashMs = 150;

// Pattern levels (0..1) from the current time.
float dim(uint32_t) { return 0.25f; }
float blinkSlow(uint32_t t) { return (t % 1000) < 500 ? 1.0f : 0.0f; }
float blinkFast(uint32_t t) { return (t % 250) < 125 ? 1.0f : 0.0f; }
float breathe(uint32_t t) {  // triangle wave, 2 s period, 10..100 %
  const float x = (t % 2000) / 1000.0f;
  return 0.1f + 0.9f * (x < 1 ? x : 2 - x);
}
float breatheDim(uint32_t t) { return breathe(t) * 0.3f; }

struct Look {
  uint8_t r, g, b;
  float (*pattern)(uint32_t);
};
constexpr Look kLooks[6] = {
    {255, 0, 0, blinkFast},    // kCritical
    {160, 0, 255, blinkSlow},  // kSetupAp
    {0, 0, 255, blinkSlow},    // kWifiConnecting
    {255, 120, 0, breathe},    // kCharging
    {255, 50, 0, blinkSlow},   // kLowBattery
    {0, 200, 255, dim},        // kClientConnected
};

}  // namespace

void Led::begin() {
  // Common anode: HIGH = off. Drive HIGH before PWM attaches to avoid a flash.
  for (uint8_t i = 0; i < 3; i++) {
    pinMode(kPins[i], OUTPUT);
    digitalWrite(kPins[i], HIGH);
    ledcAttach(kPins[i], cfg::kLedPwmHz, 8);
    ledcWrite(kPins[i], 255);
  }
  brightness_ = cfg::kLedDefaultBrightness;
  writeOnboard(0, 0, 0);
}

void Led::set(Status s, bool on) {
  if (on) {
    active_ |= 1 << s;
  } else {
    active_ &= ~(1 << s);
  }
}

void Led::flash(Flash f) {
  flash_ = f;
  flashUntilMs_ = millis() + kFlashMs;
}

void Led::setBrightness(uint8_t percent) { brightness_ = min<uint8_t>(percent, 100); }

void Led::testColor(uint8_t r, uint8_t g, uint8_t b, uint32_t ms) {
  testColor_ = {r, g, b};
  testLook_ = -1;
  testUntilMs_ = millis() + ms;
}

bool Led::testStatus(const char* name, uint32_t ms) {
  static const struct {
    const char* name;
    int8_t look;
  } kNames[] = {
      {"critical", kCritical}, {"setup", kSetupAp}, {"wifi", kWifiConnecting},
      {"charging", kCharging}, {"low", kLowBattery}, {"client", kClientConnected},
      {"idle", 100},           {"flash_ok", 101},   {"flash_error", 102},
  };
  for (const auto& n : kNames) {
    if (strcmp(n.name, name) == 0) {
      testLook_ = n.look;
      testUntilMs_ = millis() + ms;
      return true;
    }
  }
  return false;
}

void Led::loop() {
  const uint32_t t = millis();
  const bool critical = active_ & (1 << kCritical);

  if (static_cast<int32_t>(testUntilMs_ - t) > 0) {
    if (testLook_ < 0) {
      write(testColor_, 1.0f);
    } else if (testLook_ == 100) {
      write({0, 255, 0}, breatheDim(t));
    } else if (testLook_ == 101) {
      write({0, 255, 0}, 1.0f);
    } else if (testLook_ == 102) {
      write({255, 0, 0}, 1.0f);
    } else {
      const Look& l = kLooks[testLook_];
      write({l.r, l.g, l.b}, l.pattern(t));
    }
    return;
  }

  if (!critical && static_cast<int32_t>(flashUntilMs_ - t) > 0) {
    static constexpr Rgb kFlashColor[] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
    write(kFlashColor[flash_], 1.0f);
    return;
  }

  for (uint8_t s = 0; s < kCount; s++) {
    if (active_ & (1 << s)) {
      write({kLooks[s].r, kLooks[s].g, kLooks[s].b}, kLooks[s].pattern(t));
      return;
    }
  }
  write({0, 255, 0}, breatheDim(t));  // idle
}

void Led::write(Rgb c, float level) {
  const float k = level * brightness_ / 100.0f;
  const uint8_t v[3] = {c.r, c.g, c.b};
  for (uint8_t i = 0; i < 3; i++) {
    ledcWrite(kPins[i], 255 - static_cast<uint8_t>(v[i] * k));  // inverted: common anode
  }
  writeOnboard(c.r * k, c.g * k, c.b * k);
}

// The onboard WS2812 takes normal (not inverted) values; only send changes.
// The Waveshare C5 LED takes RGB byte order, not the core's GRB default (red and
// green were swapped).
void Led::writeOnboard(uint8_t r, uint8_t g, uint8_t b) {
  if (r == lastOnboard_[0] && g == lastOnboard_[1] && b == lastOnboard_[2]) return;
  lastOnboard_[0] = r;
  lastOnboard_[1] = g;
  lastOnboard_[2] = b;
  rgbLedWriteOrdered(pins::kRgbOnboard, LED_COLOR_ORDER_RGB, r, g, b);
}

void Led::off() {
  writeOnboard(0, 0, 0);
  for (uint8_t i = 0; i < 3; i++) {
    ledcDetach(kPins[i]);
    pinMode(kPins[i], INPUT);
  }
}
