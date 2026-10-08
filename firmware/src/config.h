// Pin map and constants. Source: docs/PLAN.md §1.2.
#pragma once

#include <Arduino.h>

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

namespace pins {
// ESP32-C5 (Waveshare ESP32-C5-WIFI6-KIT). Reserved: 11/12 UART0 (CH343),
// 13/14 USB, 15 PSRAM, 16-22 flash, 27 onboard RGB, 28 BOOT.
// 2, 3, 7, 25 are strapping pins: only driven after boot, keep them free of
// pull-downs.

// 13.56 MHz reader on FSPI (board default SCK/MOSI/MISO): RC522, or a PN532
// in SPI mode (DIP switch 1 OFF, 2 ON) on the same pins; RST is RC522 only.
constexpr uint8_t kRfidSs = 23;
constexpr uint8_t kRfidSck = 10;
constexpr uint8_t kRfidMosi = 8;
constexpr uint8_t kRfidMiso = 9;
constexpr uint8_t kRfidRst = 24;

// I2C (INA219, board default SDA/SCL)
constexpr uint8_t kI2cSda = 0;
constexpr uint8_t kI2cScl = 1;

// RGB LED, common anode: cathodes on these pins, LOW = on
constexpr uint8_t kLedR = 2;
constexpr uint8_t kLedG = 3;
constexpr uint8_t kLedB = 7;

// Onboard WS2812 RGB LED of the Waveshare board (mirrors the status LED).
constexpr uint8_t kRgbOnboard = 27;

// BOOT button of the board (GPIO 28, external pull-up): long press = deep sleep.
constexpr uint8_t kBootButton = 28;

constexpr uint8_t kBuzzer = 25;

// RDM6300 125 kHz reader (5 V module): its TX through a 1k/2k divider to
// UART1 RX, and its 5 V switched by an NPN + P-MOSFET (HIGH = on) so it can
// take turns with the 13.56 MHz antenna.
constexpr uint8_t kRdmRx = 4;
constexpr uint8_t kRdmPower = 5;

// Onboard ETA6098 charger: no status GPIO (only the STAT LED). Battery voltage
// through the board's 200k/100k divider on BAT_ADC.
constexpr uint8_t kBatAdc = 6;  // ADC1, VBAT / 3
}  // namespace pins

namespace cfg {
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kRfidPollMs = 100;
constexpr uint32_t kTagDebounceMs = 1000;
constexpr uint8_t kTagRemovedMisses = 3;  // consecutive empty polls
constexpr uint32_t kRdmBaud = 9600;
constexpr uint32_t kLfGoneMs = 500;  // RDM6300 alone: no frame this long = tag removed
// Combined modes: 13.56 MHz and 125 kHz take turns, one antenna on at a time.
// The RDM6300 needs ~150 ms after power-up for its first frame.
constexpr uint32_t kHfSlotMs = 300;
constexpr uint32_t kLfSlotMs = 400;

constexpr uint32_t kPowerSampleMs = 1000;
constexpr uint32_t kBatteryEventMs = 5000;
constexpr uint8_t kBatteryCriticalPct = 5;
constexpr float kRestingMa = 30;  // below this, trust the voltage curve
constexpr uint32_t kCriticalSleepMs = 90000;  // critical this long -> deep sleep
constexpr uint32_t kSleepWakeS = 300;  // timer wake to re-check the battery
constexpr float kBatDividerRatio = 3.0f * 1.022f;  // 200k + 100k over 100k, ADC gain trimmed vs. multimeter (3.975 V read 3.89 V)
constexpr float kBatMinPresentV = 2.5f;  // below: no cell, report fault
constexpr uint32_t kTrendWindowMs = 45000;  // charge state from voltage trend
constexpr float kTrendRiseV = 0.020f;  // rise over the window = charging
constexpr float kFullV = 4.15f;  // charging and flat above this = full

constexpr uint32_t kButtonDebounceMs = 30;      // shorter LOW pulses are bounce
constexpr uint32_t kButtonShortMs = 600;        // a short press is at most this long
constexpr uint32_t kButtonDoubleMs = 800;       // second short press within this: deep sleep
constexpr uint32_t kButtonWifiResetMs = 10000;  // hold, release, one short press: forget Wi-Fi
constexpr uint32_t kButtonConfirmMs = 5000;     // time allowed for that confirming press

constexpr uint32_t kWatchdogS = 15;  // > read_uid max timeout (10 s)

constexpr const char* kMdnsHost = "gapura";  // http://gapura.local
// Factory defaults, printed on the label like any smart device. Users change
// them from the dashboard (Settings); `info.defaultPassword` flags the default.
constexpr const char* kDefaultPassword = "rfid1234";  // API token + setup AP
constexpr uint32_t kDefaultBlePasskey = 123456;
constexpr uint32_t kWifiConnectTimeoutMs = 20000;

constexpr uint32_t kLedPwmHz = 5000;
constexpr uint8_t kLedDefaultBrightness = 60;  // percent
}  // namespace cfg
