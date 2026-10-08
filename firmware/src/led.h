// RGB status LED (common anode). Highest-priority active status wins.
// Priority table: docs/PLAN.md §1.5.
#pragma once

#include <Arduino.h>

class Led {
 public:
  // Persistent conditions, in priority order (lower value = higher priority).
  enum Status : uint8_t {
    kCritical,
    kSetupAp,
    kWifiConnecting,
    kCharging,
    kLowBattery,
    kClientConnected,
    kCount
  };
  // One-shot flashes; they override everything except kCritical.
  enum Flash : uint8_t { kError, kTagOk, kWriteOk };

  void begin();
  void loop();

  // Hardware test: shows a fixed colour, or one status look by name ("critical",
  // "setup", "wifi", "charging", "low", "client", "idle", "flash_ok", "flash_error"),
  // for `ms`. testStatus returns false for an unknown name.
  void testColor(uint8_t r, uint8_t g, uint8_t b, uint32_t ms);
  bool testStatus(const char* name, uint32_t ms);

  void set(Status s, bool on);
  void flash(Flash f);
  void setBrightness(uint8_t percent);
  // Releases the pins (high-Z): the LED stays dark during deep sleep.
  void off();

 private:
  struct Rgb {
    uint8_t r, g, b;
  };
  void write(Rgb c, float level);
  void writeOnboard(uint8_t r, uint8_t g, uint8_t b);

  uint8_t active_ = 0;  // bit per Status
  Flash flash_ = kError;
  uint32_t flashUntilMs_ = 0;
  uint8_t brightness_ = 60;

  // test override
  uint32_t testUntilMs_ = 0;
  Rgb testColor_{0, 0, 0};
  int8_t testLook_ = -1;  // index into the status looks, 100 = idle, 101/102 = flashes, -1 = fixed colour
  uint8_t lastOnboard_[3] = {255, 255, 255};
};
