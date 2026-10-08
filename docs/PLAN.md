# Gapura: Implementation Plan

> 🇮🇩 Versi Bahasa Indonesia: [PLAN.id.md](PLAN.id.md)

Battery-powered ESP32 + MFRC522 (RC522) RFID reader/writer, reachable over **Wi-Fi (LAN)**, **Bluetooth Low Energy** and **USB**, with an RGB status LED, INA219 battery monitoring and a Li-ion charger with charge-state detection.

Status: **phases 0–7 implemented and verified on hardware** (see `CLAUDE.md` for what is still untested). Each phase below is one branch and one pull request. 🔀 marks work that can run in parallel.

---

## 1. Components

### 1.1 Hardware

| # | Component | Role | Interface |
|---|---|---|---|
| 1 | Waveshare ESP32-C5-WIFI6-KIT (ESP32-C5-WROOM-1, 16 MB flash, 8 MB PSRAM) | MCU, dual-band Wi-Fi 6, BLE 5, USB-serial (CH343) | – |
| 2 | MFRC522 (RC522) 13.56 MHz | Read/write MIFARE Classic 1K, Ultralight/NTAG | SPI (VSPI) |
| 3 | RGB LED, **common anode**, 3 × 220 Ω | Device status indicator (anode → 3V3, cathodes → GPIO, active-low PWM) | 3 × LEDC PWM |
| 4 | INA219 breakout (0.1 Ω shunt, addr `0x40`), **optional** | Battery current and power (voltage also comes from the board ADC) | I²C |
| 5 | Onboard ETA6098 charger + MX1.25 battery header (on the Waveshare board) | Charges the 1S Li-ion cell from USB; STAT LED only, no status GPIO | BAT_ADC → GPIO 6 |
| 6 | 18650 Li-ion 1S 3.7 V, **2000 mAh** | Power source | – |
| 7 | Onboard MP1605 buck (3.3 V, 2 A, input 2.3–5.5 V) | System supply from USB or battery (no external converter needed) | – |
| 8 | Active buzzer (optional) | Audible scan feedback | GPIO |
| 9 | Battery divider 200 kΩ / 100 kΩ (onboard) | Battery voltage sense, VBAT / 3 | ADC |
| 10 | PN532 (Elechouse V3 style), **alternative to the RC522** | 13.56 MHz like the RC522 (MIFARE Classic, card security), longer range | SPI, same pins |
| 11 | RDM6300 125 kHz (e.g. WANGCL), **optional** | EM4100 tags, UID only (read-only) | UART RX, 9600 baud |
| 12 | MT3608 boost (5.0 V), 1 kΩ / 2 kΩ divider, AO3401 P-MOSFET + BC547 NPN | 5 V for the RDM6300 on battery, its TX down to 3.3 V, and its power switch in combined modes | – |

Confirmed: common-anode RGB LED, 18650 2000 mAh, MIT license.

### 1.2 Pin map

| Function | ESP32-C5 GPIO | Notes |
|---|---|---|
| RC522 SS / SDA | 23 | |
| RC522 SCK | 10 | board default FSPI |
| RC522 MOSI | 8 | board default FSPI |
| RC522 MISO | 9 | board default FSPI |
| RC522 RST | 24 | |
| RC522 VCC | 3V3 | **never 5 V** |
| I²C SDA (INA219) | 0 | board default |
| I²C SCL (INA219) | 1 | board default |
| RGB – R / G / B (cathodes) | 2 / 3 / 7 | LEDC, 5 kHz, 8-bit, inverted (duty 255 = off); strapping pins, driven only after boot |
| Buzzer | 25 | strapping pin; drive through a transistor, no pull-down on the pin |
| Battery voltage (BAT_ADC) | 6 (ADC1) | onboard 200 kΩ/100 kΩ divider (VBAT / 3), via 0 Ω R39 |

| PN532 SCK / MISO / MOSI / SS | 10 / 9 / 8 / 23 | instead of the RC522, same bus; SPI mode (DIP 1 OFF, 2 ON), VCC 3V3, no reset line |
| RDM6300 TX → UART1 RX | 4 | through R1 1 kΩ, with R2 2 kΩ to GND (5 V → 3.3 V) |
| RDM6300 power switch | 5 | HIGH = on: R3 1 kΩ to the BC547 base (R5 100 kΩ to GND), collector pulls the AO3401 gate (R4 10 kΩ to 5 V) |

Reserved on the Waveshare ESP32-C5-WIFI6-KIT: 11/12 UART0 (CH343, COM port), 13/14 native USB, 15 PSRAM, 16–22 flash, 27 onboard RGB, 28 BOOT button.

### 1.2a Reader modes

`reader.mode` in the dashboard Settings (Card reader), applied after a restart. Wiring per mode: [`docs/wiring/`](wiring/).

| Mode | Fitted | Works | Wiring |
|---|---|---|---|
| `rc522` (default) | RC522 | UID, blocks, card security | [rc522.svg](wiring/rc522.svg) |
| `pn532` | PN532 | same as the RC522 | [pn532.svg](wiring/pn532.svg) |
| `rdm6300` | RDM6300 | 125 kHz UID only; MIFARE commands answer `NO_READER` | [rdm6300.svg](wiring/rdm6300.svg) |
| `rc522+rdm6300` | RC522 + RDM6300 | both; 13.56 MHz features on 13.56 MHz cards | [rc522-rdm6300.svg](wiring/rc522-rdm6300.svg) |
| `pn532+rdm6300` | PN532 + RDM6300 | both; 13.56 MHz features on 13.56 MHz cards | [pn532-rdm6300.svg](wiring/pn532-rdm6300.svg) |

Combined modes take turns so only one antenna is on: 300 ms 13.56 MHz (field on, RDM6300 unpowered), then 400 ms 125 kHz (field off, RDM6300 powered through GPIO 5). A 125 kHz tag is reported removed after a slot without its frame (500 ms without a frame when the RDM6300 is alone). A MIFARE command switches to the 13.56 MHz slot at once. The RDM6300 runs on 5 V: an MT3608 boost from 3V3 (the board 5V pin is USB only), with its 5 V TX divided to 3.3 V. 125 kHz tags carry no credential: webhook and keyboard send their UID (keyboard only while card security is off). RC522 + PN532 together is not offered (same bus and chip select).

### 1.3 Power path

```
USB 5V ──► ETA6098 (onboard) ──► BAT ──► Li-ion cell (+) on the MX1.25 header (pin 1 = BAT, pin 2 = GND)
                                  BAT ──► board supply (3V3)
                                  BAT ──► 200k/100k divider ──► GPIO 6 (BAT_ADC)
```

The onboard charger has **no status pin** (only its STAT LED). Battery voltage is read on GPIO 6 (×3). An INA219 is optional: put it **only in the cell branch** (between the charger output and the cell) and the **sign of the current tells the direction**: positive = discharging, negative = charging.

### 1.4 Charge-state logic

| Condition | State reported |
|---|---|
| Voltage < 2.5 V (no cell) | `fault` |
| With INA219: current < −20 mA | `charging` |
| With INA219: current > +20 mA | `discharging` |
| Without INA219: voltage rose > 20 mV over 45 s | `charging` |
| Without INA219: voltage fell > 10 mV over 45 s | `discharging` |
| Charging and ≥ 4.15 V, flat | `full` |

Without an INA219 the state is an **estimate** from the voltage trend (load spikes can fool it).

State of charge (%) = Li-ion voltage-curve lookup (resting voltage), smoothed with coulomb counting from INA219 current. `low` < 15 %, `critical` < 5 % (device enters deep sleep, RFID off).

### 1.5 RGB status (highest priority wins)

| Priority | Condition | Color / pattern |
|---|---|---|
| 1 | Critical battery | red, fast blink |
| 2 | Error (command failed, RC522 not found) | red, 1 flash |
| 3 | Tag read OK / write OK | green / blue, 1 flash |
| 4 | Wi-Fi setup AP active | purple, slow blink |
| 5 | Wi-Fi connecting | blue, slow blink |
| 6 | Charging | amber, breathing |
| 7 | Low battery | orange, slow blink |
| 8 | Client connected (WS/BLE/USB) | cyan, dim solid |
| 9 | Idle, ready | green, dim breathing |

Brightness is configurable (`led.brightness`, 0–100). Patterns are non-blocking (driven from `millis()`). Common anode: the LED lights when the GPIO is LOW, so PWM is inverted and all pins are driven HIGH at boot to avoid a flash.

---

## 2. Software architecture

### 2.1 Firmware modules

| Module | Responsibility |
|---|---|
| `rfid` | Reader modes and time slots, card polling (100 ms), debounce, read/write block, dump, card security; blocks writes to block 0 and sector trailers |
| `hf` | 13.56 MHz chip interface: `Rc522Reader` (`hf_rc522`), `Pn532Reader` (`hf_pn532`, own SPI framing) |
| `rdm6300` | RDM6300 UART frames (EM4100), power switch |
| `power` | INA219 sampling (1 Hz), charger pins, state machine, SoC %, low/critical thresholds |
| `led` | RGB state machine with priorities and non-blocking patterns |
| `router` | Parses JSON requests, dispatches commands, formats responses; event bus that fans out events to all links |
| `store` | Persistent config in NVS (Wi-Fi, token, device name, LED, beep, battery thresholds) |
| `net` | Wi-Fi STA + setup AP fallback, mDNS `gapura.local`, REST `POST /api/cmd`, WebSocket `/ws`, static dashboard from LittleFS |
| `ble` | NimBLE, Nordic-UART-style service, chunking and reassembly |
| `serial_link` | USB serial 115200, NDJSON |

### 2.2 Data flow

```
             ┌──────────────────────────── ESP32 ────────────────────────────┐
 RC522 ─SPI─►│ rfid ──tag──┐                                                 │
 INA219 ─I²C►│ power ─batt─┼─► event bus ─┬─► net (WS)  ───────────────────► │──► Browser / phone (LAN)
 BAT_ADC ───►│             │              ├─► ble (notify) ────────────────► │──► Phone / PC (BLE)
             │             │              └─► serial_link ─────────────────► │──► PC (USB)
             │             └─► led (RGB)                                     │
             │                                                               │
             │  net / ble / serial_link ──request──► router ──► rfid/power/store
             │                                  ◄──response──                │
             └───────────────────────────────────────────────────────────────┘
```

1. **Command:** client → any link → `router` → module → response back on the **same** link.
2. **Event:** `rfid` (tag detected/removed) or `power` (battery sample, charge-state change) → event bus → **all** connected links + `led`.
3. **Status:** `net`, `ble`, `router` errors also publish to `led`.
4. Only one RFID operation runs at a time (mutex); a concurrent request gets `BUSY`.

### 2.3 Protocol

One JSON protocol on every link. Contract: [PROTOCOL.md](PROTOCOL.md). New in this revision:

- Command `battery` → `{ v, mA, mW, pct, state }`
- `info` now includes `battery`
- Event `battery` every 5 s while a client is connected, and immediately on charge-state change
- Config keys `led.brightness`, `battery.lowPct`, `battery.capacityMah` (default 2000)

### 2.4 Dashboard

Vite + TypeScript, no UI framework (small bundle for LittleFS). Clean, modern look:

- System font stack (works offline, no CDN), light/dark theme from `prefers-color-scheme`
- Card-based layout, 8 px spacing grid, one accent color, rounded corners, subtle shadows
- Persistent top bar: connection chip (Wi-Fi / BLE / USB), **battery chip** (%, charging bolt, color by level), LED status mirror
- Views: **Connect**, **Scan** (live tag feed), **Card** (read/write block, dump grid 16 × 4, trailer warnings), **Power** (voltage, current, SoC, charge state, sparkline of the last 10 min), **Settings**
- Mobile-first, installable as PWA

---

## 3. File list (compact structure)

```
Wireless RFID Reader/
├─ CLAUDE.md                 context for AI assistants & contributors (EN + ID)
├─ README.md / README.id.md  project overview, quick start
├─ LICENSE                   MIT
├─ docs/
│  ├─ PLAN.md / PLAN.id.md   this plan
│  ├─ PROTOCOL.md            JSON contract shared by firmware & dashboard
│  ├─ wiring/                wiring per reader mode (SVG, from tools/wiring.py)
│  └─ hardware/              photos, schematic (Phase 0)
├─ firmware/                 PlatformIO, Arduino framework
│  ├─ platformio.ini
│  ├─ src/
│  │  ├─ main.cpp            setup/loop, module wiring
│  │  ├─ config.h            pins, constants, FW_VERSION
│  │  ├─ rfid.h/.cpp        reader modes, MIFARE, card security
│  │  ├─ hf.h, hf_rc522.cpp, hf_pn532.cpp
│  │  ├─ rdm6300.h/.cpp
│  │  ├─ power.h/.cpp
│  │  ├─ led.h/.cpp
│  │  ├─ router.h/.cpp
│  │  ├─ commands.h/.cpp    protocol commands → modules
│  │  ├─ util.h            pure helpers: SoC curve, MIFARE rules, hex
│  │  ├─ store.h/.cpp
│  │  ├─ net.h/.cpp
│  │  ├─ ble.h/.cpp
│  │  └─ serial_link.h/.cpp
│  ├─ data/                  dashboard build output (LittleFS, git-ignored)
│  └─ test/test_router/      native unit tests (no hardware)
├─ dashboard/                Vite + TypeScript
│  ├─ index.html
│  ├─ package.json, vite.config.ts, tsconfig.json
│  └─ src/
│     ├─ main.ts             app shell & routing
│     ├─ device.ts           client: id matching, events, state
│     ├─ ui.ts               DOM helpers, icons, toasts
│     ├─ protocol.ts         request/response/event types
│     ├─ transport/          ws.ts, ble.ts, serial.ts, mock.ts
│     ├─ views/              connect, scan, card, power, settings
│     └─ style.css           design tokens, light/dark
└─ tools/
   ├─ rfid_cli.py            test client over USB / Wi-Fi
   └─ wiring.py              generates docs/wiring/*.svg
```

---

## 4. Phases

| Phase | Scope | Done when |
|---|---|---|
| **0 Setup** | Repo, license, `.gitignore`, wiring on breadboard, photos in `docs/hardware/` | `pio run` builds an empty sketch and flashes |
| **1 RFID over USB** | `config.h`, `rfid`, UID events on serial | Tapping a card prints `{"event":"tag",...}`; RC522 version 0x91/0x92 |
| **2 Power + LED** 🔀 | `power` (INA219 + charger pins + SoC), `led` state machine | Serial shows correct V/mA; unplugging USB flips `charging`→`discharging`; LED follows the priority table |
| **3 Router + USB** | `router`, `serial_link`, all commands incl. `battery`; `tools/rfid_cli.py` 🔀; native tests 🔀 | Every command in PROTOCOL.md works from the CLI; write-then-read block 4 matches |
| **4 Wi-Fi** | `store`, `net` (STA/AP, mDNS, REST, WS, token) | `curl` to `gapura.local` works; WS client receives `tag` and `battery` events |
| **5 BLE** | `ble` (NimBLE, chunking, passkey) | nRF Connect sends `ping` and receives events; free heap logged with Wi-Fi + BLE on |
| **6 Dashboard** 🔀 (can start at Phase 3 with `mock.ts`) | All views, battery chip, Power view, PWA, build → `firmware/data/` | Served from `gapura.local`; local dev connects via Web Serial and Web Bluetooth |
| **7 Hardening** | Mutex, watchdog, Wi-Fi reconnect, OTA (token-protected), deep sleep on critical battery, CI | CI green; 30-min stress test (scan + WS + BLE) without reboot; runtime on battery measured |
| **8 Optional** | USB HID keyboard mode (ESP32-S3), MQTT/webhook, Flutter app, scan history backend | – |

---

## 5. Risks

- **RAM:** Wi-Fi + BLE + web server is heavy → NimBLE, `huge_app` partition, watch `info.heap`.
- **Card bricking:** writing sector trailers can lock a card permanently → blocked by default.
- **Brown-out:** RC522 + Wi-Fi TX peaks ~300 mA; a weak cell or small boost converter causes resets → use a buck-boost ≥ 1 A and a 470 µF capacitor near the ESP32.
- **Cell without protection:** use a cell or pack with a protection circuit (the onboard charger does not disconnect the load at low voltage).
- **MX1.25 polarity:** pin 1 = BAT (+), pin 2 = GND; check it against the board silkscreen before plugging in a pack.
- **No charger wake pin:** deep sleep wakes on a timer (300 s) and re-checks the battery.
- **Web Bluetooth** is not available in iOS Safari; **Web Serial** only in desktop Chrome/Edge.
- **Factory-default password** (`rfid1234`, BLE passkey `123456`) is public in the repo → the dashboard shows a warning until it is changed in Settings.
- **MIFARE Classic Crypto1 is insecure** → not for payment or high-security access control.

---

## 6. Open questions

1. UI design reference (mentioned as attached, not received yet).
2. Battery voltage calibration of the GPIO 6 divider against a multimeter.
