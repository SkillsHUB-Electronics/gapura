# Contributing to Gapura

> 🇮🇩 Bahasa Indonesia: [CONTRIBUTING.id.md](CONTRIBUTING.id.md)

Thanks for helping. Gapura is hardware plus software, so useful contributions include bug reports with serial logs, photos of a working wiring, tests on other boards or readers, documentation fixes, and firmware or dashboard changes.

By taking part you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## Before you start

- Read the [README](README.md), [docs/PLAN.md](docs/PLAN.md) (architecture and remaining work) and [docs/PROTOCOL.md](docs/PROTOCOL.md) (the JSON contract).
- Look for an existing [issue](https://github.com/SkillsHUB-Electronics/gapura/issues) first. For anything larger than a small fix, open an issue to agree on the approach before writing code.
- Open work is listed in [docs/PLAN.md §4.1](docs/PLAN.md#41-remaining-work). One item there is one issue and one pull request.

## Development setup

Prerequisites: VS Code + PlatformIO, Node.js 20+, Python 3.10+, Chrome or Edge.

```bash
git clone https://github.com/<your-username>/gapura.git
cd gapura

# Firmware: build, flash, unit tests
cd firmware
pio run                    # build
pio run -t upload          # flash over USB
pio test -e native         # host unit tests (needs gcc/g++)

# Dashboard
cd ../dashboard
npm ci
npm run dev                # http://localhost:5173/?demo uses a simulated reader
npm run build              # type-check and build into firmware/data/
```

On Windows, keep paths short: `set PLATFORMIO_CORE_DIR=C:\pio` and `set PLATFORMIO_WORKSPACE_DIR=C:\pio-ws\gapura`.

No hardware? The dashboard's mock transport (`dashboard/src/transport/mock.ts`, `?demo` in the URL) and `pio test -e native` cover most changes.

## Workflow

1. Fork the repository and create a branch from `main`: `feat/<topic>`, `fix/<topic>` or `docs/<topic>`.
2. Commit with [Conventional Commits](https://www.conventionalcommits.org/): `feat(firmware): …`, `fix(dashboard): …`, `docs: …`.
3. Keep each pull request small and focused on one change.
4. Open the pull request against `main` and fill in the template. CI must pass and one maintainer review is required.
5. Add a line to the `Unreleased` section of [CHANGELOG.md](CHANGELOG.md) when users will notice the change.

## Project rules

These keep the firmware stable and the links consistent.

- **Protocol first.** A new or changed command updates `docs/PROTOCOL.md` and `dashboard/src/protocol.ts` before the firmware or UI code. Protocol changes need one firmware and one dashboard review.
- **Non-blocking firmware.** No long `delay()` in `loop()`; use `millis()`. RFID/SPI work stays on the main loop task.
- **Pure modules stay pure.** `router` and `util.h` must not include Arduino headers, so they keep running in `pio test -e native`.
- **Transport boundary.** The dashboard UI never talks to WebSocket, BLE or Serial directly, only through the `Transport` interface and `device.ts`.
- **Card safety.** Never allow writes to block 0 or sector trailers outside the dedicated card-security commands.
- **Docs are bilingual.** Each doc has an English file and an Indonesian `.id.md` file with the same structure. Code, identifiers and commit messages are in English.
- **Style.** C++: `clang-format` (Google style, indent 2). TypeScript: strict mode.

## Never commit secrets

Only the factory defaults (`kDefaultPassword`, `kDefaultBlePasskey` in `firmware/src/config.h`) belong in the repository. Put your own Wi-Fi details and keys in `firmware/src/secrets.h`, which is git-ignored. Never commit card-security backups, card secrets, real card numbers, company data or recovery codes. Use made-up UIDs and IDs in examples and tests.

## Reporting bugs and requesting features

Use the [issue forms](https://github.com/SkillsHUB-Electronics/gapura/issues/new/choose). For bugs, include the board, reader mode, firmware version (`info` command), the link you used (Wi-Fi, BLE or USB) and the serial log. Security problems go through [SECURITY.md](SECURITY.md), not public issues.

## License

By contributing, you agree that your contributions are licensed under the [MIT License](LICENSE).
