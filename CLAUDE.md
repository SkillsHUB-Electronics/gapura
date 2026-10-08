# CLAUDE.md

Project context for AI assistants and new contributors. Replaces the old `SUMMARY.md`. Keep it short and current.
*Konteks proyek untuk AI assistant dan kontributor baru. Menggantikan `SUMMARY.md` lama. Jaga tetap singkat dan terbaru.*

## Project / Proyek

**Name: Gapura** (chosen 2026-10-01). Since 2026-10-08 the device name is `Gapura-XXXX` (setup hotspot `Gapura-Setup-XXXX`, BLE name) and the mDNS host is `gapura.local`; stored `RFID-XXXX` names migrate on boot. The GitHub repo name is unchanged until the maintainer confirms. / *Nama perangkat kini `Gapura-XXXX`, hotspot setup `Gapura-Setup-XXXX`, mDNS `gapura.local`; nama lama `RFID-XXXX` dimigrasi saat boot. Nama repo belum diubah.*

Battery-powered ESP32-C5 + RC522 RFID reader/writer, reachable over Wi-Fi (REST + WS, `gapura.local`), BLE (NimBLE) and USB serial (NDJSON), all using one JSON protocol. RGB status LED, battery voltage on the board ADC (GPIO 6), onboard ETA6098 charger, optional INA219. Web dashboard (Vite + TS) served from LittleFS.

*Pembaca/penulis RFID ESP32 + RC522 bertenaga baterai, diakses lewat Wi-Fi, BLE, dan USB dengan satu protokol JSON. LED RGB status, tegangan baterai lewat ADC board (GPIO 6), charger ETA6098 onboard, INA219 opsional. Dashboard web di-host di ESP32.*

**Status (2026-10-01):** Phases 0–7, card security and the signed credential are merged to `main` (PRs #1, #2, #4 in `SkillsHUB-Electronics/gapura-reader`, private). ESP32-C5, partitions `default_16MB`. **Verified on hardware (Waveshare C5, COM11):** USB NDJSON, RC522 (clone `0xB2`, max RX gain), Wi-Fi STA + REST/WS/mDNS, OTA (flaky for LittleFS: retry), BLE app mode and BLE HID keyboard mode, battery on ADC, webhook (`credential`, `verified`, server replies in Settings), card security: per-card keys, credential of up to 95 characters in blocks 4-6 and 8-10 with its HMAC in block 12, reset to factory (also with another secret), on/off switch, encrypted or plain secret backup file, keyboard types the credential. The Secure ID was removed. The card secret is kept by the maintainer outside the repo (never commit backups or recovery codes; `.gitignore` covers them). Web Bluetooth/Serial and backup encryption need a secure context (HTTPS or `localhost`). HTTPS webhook verified against httpbin.org with BLE keyboard mode on (5/5 HTTP 200, 2.2–3.3 s, 2026-10-08): `webhook.cpp` routes mbedTLS blocks ≥ 1 KB to PSRAM via `mbedtls_platform_set_calloc_free`, because the prebuilt SDK keeps mbedTLS in internal RAM (about 50 KB free with BLE on). 30-min stress over Wi-Fi: no reboot seen, min heap 45 KB, 7 WebSocket drops (cause open). Not yet tested: HTTPS to the company server, BAD_SIGNATURE tamper test with the production secret, battery `low` warning and deep sleep, INA219, LED/buzzer wiring, 30-min stress test. Native tests run in CI only (no host gcc). Next: dashboard redesign (separate thread), then public hosting (GitHub Pages needs a public repo). / *Sudah dites di C5: USB, RC522, Wi-Fi, OTA, BLE, keyboard HID, webhook, pengamanan kartu dengan credential 95 karakter. Belum: HTTPS webhook ke server nyata, peringatan baterai rendah dan deep sleep, INA219, LED/buzzer, uji stres. Berikutnya: redesain dashboard, hosting publik.*

Build (Windows needs short `PLATFORMIO_WORKSPACE_DIR` and `PLATFORMIO_CORE_DIR=C:\pio`, see README): `cd firmware && python -m platformio run` · test: `python -m platformio test -e native` · CLI: `python tools/rfid_cli.py --port COM11 ping`

## Source of truth / Acuan utama

| File | Purpose |
|---|---|
| `docs/PLAN.md` · `docs/PLAN.id.md` | components, pin map, power path, data flow, phases |
| `docs/PROTOCOL.md` | JSON contract; firmware and dashboard must match it |
| `README.md` · `README.id.md` | overview, quick start, contributing |

When a decision changes, update PLAN (both languages) and the table below in the same commit.
*Jika keputusan berubah, perbarui PLAN (dua bahasa) dan tabel di bawah dalam commit yang sama.*

## Key decisions / Keputusan utama

| # | Decision | Why |
|---|---|---|
| 1 | PlatformIO + Arduino (MFRC522, ArduinoJson 7, ESPAsyncWebServer, NimBLE-Arduino, Adafruit INA219) | locked deps, CI, mature libraries |
| 2 | BLE (NimBLE), not Classic SPP | iOS + Web Bluetooth support, lower RAM |
| 3 | One JSON protocol for all links | one client codebase |
| 4 | REST for commands, WebSocket for events | real-time without polling |
| 5 | Dashboard hosted on ESP32, no UI framework, system fonts | small bundle, works offline |
| 6 | Block 0 and sector trailers write-protected | prevent bricked cards |
| 7 | RC522 on board-default FSPI pins, I²C on 0/1 | board defaults on the C5 |
| 8 | Optional INA219 in the cell branch only | current sign = charge (−) / discharge (+) |
| 9 | Battery voltage from BAT_ADC (GPIO 6, ÷3 divider); charge state estimated from INA219 current or the voltage trend | the onboard ETA6098 has no status pin |
| 10 | Common-anode RGB LED via inverted LEDC PWM, priority-based patterns | one indicator for all states |
| 11 | Onboard charger (replaces TP4056, 2026-09-30), 18650 2000 mAh, MIT license | schematic shows ETA6098 + MX1.25 header |
| 12 | ESP32Async web server; requests queued and run in `loop()` | RFID/SPI stays on one task, HTTP replies deferred with `pause()` |
| 13 | BLE passkey pairing (6 digits in NVS, default 123456), events only to paired client | tag UIDs not leaked to unpaired phones |
| 20 | `mode: "off"` disables Bluetooth (about +40 KB free RAM); HTTPS webhook also works with BLE on because `webhook.cpp` routes mbedTLS blocks ≥ 1 KB to PSRAM | the prebuilt Arduino libs keep mbedTLS in internal RAM (`CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC`); PR #14 |
| 19 | Reader modes `rc522` (default), `pn532`, `rdm6300`, `rc522+rdm6300`, `pn532+rdm6300` (`reader.mode`, after reboot); PN532 on the RC522 SPI pins with own framing; RDM6300 TX on GPIO 4 via 1k/2k divider, 5 V from an MT3608 switched by GPIO 5; combined modes alternate 300/400 ms slots, one antenna on | maintainer request 2026-10-07; card security stays 13.56 MHz MIFARE Classic, 125 kHz is UID only |
| 18 | Factory-default device password `rfid1234` (API token + WPA2 setup AP), changeable in dashboard Settings; `info.defaultPassword` drives a warning banner | like consumer smart devices; maintainer request 2026-09-30 |
| 15 | Task watchdog restarted at 15 s with reset; blocking waits feed it | `read_uid` can block up to 10 s |
| 16 | Deep sleep after 90 s of critical battery, timer wake every 300 s to re-check | protects the cell; no charger wake pin on the C5 board |
| 17 | ESP32-C5 (Waveshare ESP32-C5-WIFI6-KIT) on pioarduino platform, Arduino core 3 | user's actual board; C5 needs core 3, which only pioarduino ships |
| 14 | `default_16MB` partitions (changed from `min_spiffs` 2026-10-01) | two 6.4 MB app slots for OTA, 3.4 MB LittleFS; the webhook TLS stack no longer fit 1.9 MB. Changing the table needs one USB flash (firmware + `uploadfs`) |

## Pins / Pin

Board: Waveshare ESP32-C5-WIFI6-KIT (CH343 on COM11). RC522 SS 23 · SCK 10 · MOSI 8 · MISO 9 · RST 24 · I²C SDA 0 · SCL 1 · RGB 2/3/7 · buzzer 25 · BAT_ADC 6 · PN532 on the RC522 SPI pins · RDM6300 TX → GPIO 4 (1k/2k divider), power switch GPIO 5. Wiring per reader mode: `docs/wiring/`. Reserved: 11/12 UART0, 13/14 USB, 15 PSRAM, 16–22 flash, 27 onboard RGB, 28 BOOT.

## Conventions / Konvensi

- Firmware modules are flat files in `firmware/src/` (`rfid`, `power`, `led`, `router`, `commands`, `store`, `net`, `ble`, `serial_link`, `util.h`). `router` and `util.h` stay free of Arduino headers so they run in `pio test -e native`. No blocking `delay()` in the loop.
- Dashboard talks to the device only through the `Transport` interface (`ws`, `ble`, `serial`, `mock`) and `device.ts`. UI text is English. `?demo` in the URL connects to the mock.
- Protocol first: change `docs/PROTOCOL.md` + `dashboard/src/protocol.ts` before firmware/UI code.
- Docs are bilingual: English file + `.id.md` Indonesian file with the same structure. Code, identifiers and commit messages in English.
- Never commit real secrets; use `firmware/src/secrets.h` (git-ignored). Only factory defaults (`kDefaultPassword`, `kDefaultBlePasskey` in `config.h`) are in the repo.

## Working with the maintainer / Cara kerja dengan maintainer

- Show only the changed parts of code, not whole files. / *Tampilkan hanya bagian kode yang berubah.*
- Keep explanations short unless asked. / *Penjelasan singkat kecuali diminta.*
- Ask first when an instruction is ambiguous. / *Tanya dulu jika instruksi ambigu.*
- Replies to the maintainer in Indonesian. / *Balas maintainer dalam Bahasa Indonesia.*

## Open questions / Pertanyaan terbuka

UI design reference (not received yet) · GPIO 6 battery-voltage calibration with a multimeter.
