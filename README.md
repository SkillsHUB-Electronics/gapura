# Gapura

*Wireless RFID reader/writer: ESP32-C5 + RC522 over Wi-Fi, Bluetooth LE and USB.*

> 🇮🇩 Bahasa Indonesia: [README.id.md](README.id.md)

Open-source, battery-powered **ESP32 + RC522** RFID reader/writer that any phone or PC can use over **Wi-Fi**, **Bluetooth LE** or **USB**, with one JSON protocol and a built-in web dashboard.

**Status:** firmware and dashboard run on hardware (Waveshare ESP32-C5). **Online dashboard:** <https://skillshub-electronics.github.io/gapura/> (connects over Bluetooth or USB in Chrome/Edge, or `?demo`). See [docs/PLAN.md](docs/PLAN.md) and [CLAUDE.md](CLAUDE.md).

## Features

- Read/write MIFARE Classic 1K and NTAG/Ultralight (13.56 MHz)
- Three links, one protocol: Wi-Fi (REST + WebSocket, `http://gapura.local`), BLE (Web Bluetooth / mobile), USB serial (Web Serial / scripts)
- Battery monitoring with INA219 (voltage, current, %), charge-state detection (charging / full / discharging)
- RGB LED status indicator
- Clean, mobile-first web dashboard served from the device itself
- Safe writes: block 0 and sector trailers are protected
- Over-the-air updates (firmware and dashboard), watchdog, deep sleep on critical battery

## Hardware

Waveshare ESP32-C5-WIFI6-KIT · RC522 or PN532 (13.56 MHz) and/or RDM6300 (125 kHz) · optional INA219 · onboard charger · 18650 2000 mAh cell (MX1.25 header) · common-anode RGB LED · optional buzzer.
Pin map and power path: [docs/PLAN.md §1](docs/PLAN.md#1-components). Wiring for each reader mode (RC522, PN532, RDM6300, RC522 + RDM6300, PN532 + RDM6300): [docs/wiring/](docs/wiring/); pick the mode in the dashboard under Settings → Card reader.

## Repository layout

```
docs/        plan, protocol, hardware
firmware/    ESP32 firmware (PlatformIO, Arduino)
dashboard/   web dashboard (Vite + TypeScript)
tools/       test client (Python)
```

## Quick start

Prerequisites: VS Code + PlatformIO, Node.js 20+, Python 3.10+, Chrome/Edge (Web Serial & Web Bluetooth).

```bash
# firmware
cd firmware && pio run -t upload && pio device monitor
# dashboard
cd dashboard && npm ci && npm run dev      # open http://localhost:5173/?demo for a simulated reader
npm run build && cd ../firmware && pio run -t uploadfs
```

First boot without Wi-Fi config opens the hotspot `Gapura-Setup-XXXX` (password `rfid1234`) → browse to `http://192.168.4.1`, enter your Wi-Fi SSID and password → open `http://gapura.local` and sign in with the default device password `rfid1234` (Bluetooth passkey `123456`). Change both in **Settings**.

## Protocol

```jsonc
{ "id": 1, "cmd": "battery" }
{ "id": 1, "ok": true, "data": { "v": 3.92, "mA": 145, "pct": 78, "state": "discharging" } }
```

Full contract: [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Contributing

1. Pick a task from `docs/PLAN.md` (one phase item = one issue), assign yourself.
2. Branch `feat/<phase>-<topic>`, commit with [Conventional Commits](https://www.conventionalcommits.org/) (`feat(firmware): …`, `fix(dashboard): …`).
3. Open a PR to `main`; CI must pass and one review is required. Protocol changes need one firmware **and** one dashboard reviewer.
4. Never commit real secrets (your Wi-Fi, changed passwords); only the factory defaults live in `config.h`. Use `firmware/src/secrets.h` (git-ignored).
5. No hardware? Use `dashboard/src/transport/mock.ts` and `pio test -e native`.

Code style: C++ `clang-format` (Google, indent 2), non-blocking loop (`millis()`, no long `delay()`); TypeScript strict + ESLint + Prettier; UI never talks to WebSocket/BLE/Serial directly, only through the `Transport` interface.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Installing the ESP32-C5 framework fails with `FileNotFoundError` under `.platformio\.cache` on Windows | Same path limit inside the PlatformIO home. Also set a short core dir: `$env:PLATFORMIO_CORE_DIR="C:\pio"` |
| Build fails with `No such file or directory` inside `NimBLE-Arduino` on Windows | The project path is too long (260-char limit). Build with a short workspace: `set PLATFORMIO_WORKSPACE_DIR=C:\pio-ws\wireless-rfid` (PowerShell: `$env:PLATFORMIO_WORKSPACE_DIR="C:\pio-ws\wireless-rfid"`) |
| `boot` event shows `rc522: "0x00"` or `"0xFF"` | SPI wiring or 3.3 V supply of the RC522 |
| `gapura.local` does not open | Some Android versions lack mDNS; use the IP from `info` |
| Web Bluetooth missing on iPhone | Safari has no Web Bluetooth; use Bluefy or a native app |

## Security note

MIFARE Classic (Crypto1) is not secure. Do not use this device for payments or high-security access control.

## License

[MIT](LICENSE).
