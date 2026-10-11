# Getting help with Gapura

Thanks for using Gapura. Here is where to go, depending on what you need.

## Questions and usage help

- **Docs:** start with the [README](README.md), [docs/PLAN.md](docs/PLAN.md) for hardware and pin maps, and [docs/PROTOCOL.md](docs/PROTOCOL.md) for the JSON commands.
- **Troubleshooting:** the [README troubleshooting table](README.md#troubleshooting) covers the most common build and connection problems.
- **Try without hardware:** the [online dashboard demo](https://skillshub-electronics.github.io/gapura/?demo).

Please search existing [issues](https://github.com/SkillsHUB-Electronics/gapura/issues) before asking; your question may already be answered.

## Bug reports

Open a [bug report](https://github.com/SkillsHUB-Electronics/gapura/issues/new/choose). To help us reproduce it quickly, include:

- the board and reader mode (RC522, PN532, RDM6300 or a combined mode),
- the firmware version from the `info` command,
- the link you used (Wi-Fi, Bluetooth LE or USB) and the browser or app,
- the serial log (`pio device monitor`) around the problem.

Remove passwords, card secrets and real card numbers from logs before posting.

## Feature requests

Open a [feature request](https://github.com/SkillsHUB-Electronics/gapura/issues/new/choose) that describes the problem you want to solve, not only the solution.

## Security vulnerabilities

Do **not** open a public issue. Follow [SECURITY.md](SECURITY.md).

## Contributing

Want to help? See [CONTRIBUTING.md](CONTRIBUTING.md).
