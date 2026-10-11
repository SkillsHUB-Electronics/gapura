# Security Policy

## Supported versions

Gapura is in early development. Security fixes go into the latest release and `main`.

| Version | Supported |
|---|---|
| Latest 0.x release and `main` | Yes |
| Older releases | No |

## Reporting a vulnerability

**Please do not open a public issue for security problems.**

Report it privately through GitHub: [Report a vulnerability](https://github.com/SkillsHUB-Electronics/gapura/security/advisories/new). Please include:

- what is affected (firmware, dashboard, protocol, card security) and the version (`info` command),
- the steps to reproduce,
- the impact you expect,
- a suggested fix, if you have one.

We will acknowledge the report, keep you informed while we work on it, and credit you in the release notes unless you prefer otherwise.

## Security model

Gapura is a reader for local networks and nearby devices. Knowing its limits helps you deploy it safely.

| Area | How Gapura protects it | Limits |
|---|---|---|
| Device API (REST, WebSocket, OTA) | Bearer token (device password) on every request; factory password triggers a warning banner | Served over plain HTTP on the LAN. Use a trusted network and change the default password `rfid1234` |
| Setup hotspot | WPA2 with the device password | Default password until you change it |
| Bluetooth LE | Passkey pairing (6 digits); events go only to the paired client | Default passkey `123456` until you change it |
| Webhook | HTTPS with certificate validation against the built-in CA bundle | Plain `http://` URLs are sent unencrypted |
| Card data | Per-card keys derived from a secret, credential signed with HMAC bound to the card UID; block 0 and sector trailers are write-protected | MIFARE Classic (Crypto1) itself is broken: a skilled attacker can read or clone sectors. Do not use Gapura for payments or high-security access control |
| Card secret | Kept on the device and in an optional backup file you download (encrypted with a passphrase, or plain) | Anyone with the secret can mint valid cards. Store backups offline and never commit them |

## Out of scope

- Attacks that need physical access to an unlocked device or its USB port.
- Weaknesses of MIFARE Classic Crypto1 itself.
- Deployments that keep the factory password or passkey.
