# Changelog

All notable changes to Gapura are listed here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org/). Release downloads (firmware and dashboard images) are on the [Releases](https://github.com/SkillsHUB-Electronics/gapura/releases) page.

## [Unreleased]

## [0.1.0] - 2026-10-11

First public release of Gapura in this repository.

### Added

- ESP32-C5 firmware (PlatformIO, Arduino core 3) with one JSON protocol over USB serial, Wi-Fi (REST + WebSocket, mDNS `gapura.local`) and Bluetooth LE.
- Reader modes `rc522`, `pn532`, `rdm6300`, `rc522+rdm6300` and `pn532+rdm6300`, with wiring diagrams in `docs/wiring/`.
- Read and write MIFARE Classic 1K and NTAG/Ultralight; block 0 and sector trailers are write-protected.
- Card security: per-card keys and a credential of up to 95 characters signed with HMAC, factory reset, and a secret backup file.
- HTTPS webhook on every tap and BLE HID keyboard mode that types the credential.
- Battery level and charge state, RGB status LED, buzzer, deep sleep on critical battery.
- Over-the-air updates for firmware and dashboard, task watchdog, Wi-Fi setup hotspot `Gapura-Setup-XXXX`.
- Mobile-first web dashboard (Vite + TypeScript) served from the device and published on GitHub Pages, with a `?demo` mode.
- CI for native unit tests, the firmware build and the dashboard build.
- Community files (`CONTRIBUTING`, `CODE_OF_CONDUCT`, `SECURITY`, `SUPPORT`), issue forms, a pull request template, `CODEOWNERS` and Dependabot.
- Release workflow that attaches `firmware.bin`, `bootloader.bin`, `partitions.bin` and `littlefs.bin` to each GitHub release.

### Fixed

- Dashboard on phones: the logo in the header was empty, the connection chip did not shorten long names, and the scan history header and rows wrapped badly.

[Unreleased]: https://github.com/SkillsHUB-Electronics/gapura/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/SkillsHUB-Electronics/gapura/releases/tag/v0.1.0
