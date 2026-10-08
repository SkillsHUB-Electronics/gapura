# Protocol / Protokol

Single JSON contract used by every link (USB serial, Wi-Fi REST/WebSocket, BLE). Firmware (`firmware/src/router.*`) and dashboard (`dashboard/src/protocol.ts`) must follow this file. Changes require review from one firmware and one dashboard maintainer.

*Satu kontrak JSON untuk semua jalur (USB serial, Wi-Fi REST/WebSocket, BLE). Firmware dan dashboard wajib mengikuti file ini. Perubahan butuh review dari satu maintainer firmware dan satu maintainer dashboard.*

## 1. Messages

```jsonc
// Request
{ "id": 12, "cmd": "read_block", "args": { "block": 4, "key": "FFFFFFFFFFFF", "keyType": "A" } }
// Response (success / error)
{ "id": 12, "ok": true,  "data": { "block": 4, "hex": "00112233445566778899AABBCCDDEEFF" } }
{ "id": 12, "ok": false, "error": { "code": "AUTH_FAILED", "msg": "Authentication failed" } }
// Event (no id, broadcast to every connected client)
{ "event": "boot",        "data": { "fw": "0.1.0", "name": "Gapura-A1B2", "rc522": "0x92", "reader": "rc522", "ina219": true, "ok": true } }
{ "event": "tag",         "data": { "uid": "DE AD BE EF", "type": "MIFARE 1KB", "reader": "rc522", "ts": 123456 } }
{ "event": "tag_removed", "data": { "uid": "DE AD BE EF" } }
{ "event": "battery",     "data": { "v": 3.92, "mA": 145.2, "mW": 569, "pct": 78, "state": "discharging" } }
```

## 2. Commands

| cmd | args | data |
|---|---|---|
| `ping` | – | `{ fw }` |
| `info` | – | `{ fw, name, heap, maxBlock, psramKB, uptimeS, rc522, ina219, scanning, ip, rssi, webhook, defaultPassword, reader:{mode, hf, hfOk, lf}, links:{usb, wifi, wsClients, ble}, battery }` |
| `battery` | – | `{ v, mA, mW, pct, state }` |
| `scan_start` / `scan_stop` | – | – |
| `read_uid` | `timeoutMs` | `{ uid, type }` |
| `read_block` | `block, key, keyType` | `{ block, hex }` |
| `write_block` | `block, hex (32 chars), key, keyType` | `{ block }` |
| `read_sector` | `sector, key, keyType` | `{ sector, blocks[4] }` |
| `dump` | `key, keyType` | `{ sectors: [{ sector, blocks[4] } or { sector, error }] }` |
| `write_text` | `startBlock, text, key, keyType` | `{ blocks }` |
| `config_get` | – | config object (secrets masked) |
| `config_set` | any subset of config | – |
| `led_test` | `status` (`critical`, `setup`, `wifi`, `charging`, `low`, `client`, `idle`, `flash_ok`, `flash_error`) or `r`, `g`, `b` (0 to 255); `ms` (200 to 10000, default 3000) | – |
| `webhook_test` | – | – |
| `buzzer_test` | `ms` (20 to 2000, default 300) | – |
| `wifi_scan` | – | `{ scanning, networks?: [{ ssid, rssi, channel, band, secure }] }` |
| `wifi_connect` | `ssid`, `pass` | `{ state, ssid, ip, rssi, error }` |
| `wifi_status` | – | `{ state, ssid, ip, rssi, error }` |
| `card_write_credential` | `text` (1–95 printable ASCII), `factoryKey` (optional) | `{ uid, text, keyed }` |
| `card_read_credential` | – | `{ uid, text }` |
| `card_reset` | `factoryKey` (12 hex, optional), `secret` (32 hex, optional) | `{ uid }` |
| `sleep` | – | – |
| `reboot` | – | – |

`battery.state`: `charging` · `full` · `discharging` · `fault`. `pct` is 0–100, `mA` positive = discharging, negative = charging.

`key` defaults to `FFFFFFFFFFFF`, `keyType` to `A`. MIFARE Classic only (Mini/1K/4K, blocks 0–63); other cards return `UNSUPPORTED_CARD`. `read_uid` timeout defaults to 5000 ms, max 10000.

Config (nested objects, `config_set` accepts any subset): `{ wifi:{ssid,pass}, token (≥ 8 chars), name (1–24 chars), beep, led:{brightness 0–100}, battery:{lowPct 5–50, default 15; capacityMah 100–20000, default 2000} }`. `ble:{passkey}` (6 digits, applied after reboot). `mode` (`app` default, `keyboard`, or `off` = Bluetooth fully disabled, applied after reboot; `off` frees RAM for an HTTPS webhook) and `keyboard:{source (`block` default or `uid`), block (0–63, not a trailer, default 4), key (12 hex, default `FFFFFFFFFFFF`, masked in `config_get`), keyType (`A`/`B`), enter (default true)}`.

**Buttons** (onboard): press BOOT twice quickly (a beep and a flash tell you) to put the reader into deep sleep; press RESET (EN) alone to wake it. To forget the Wi-Fi network and restart in setup mode, hold BOOT for 10 s (beep), release (beep), then press BOOT once within 5 s (three beeps). Only short presses trigger an action, because on the Waveshare C5 the USB chip's DTR line also pulls BOOT low, so a serial program holding DTR looks like a held button; a press already down at boot is ignored. Deep sleep ends the Wi-Fi, Bluetooth and USB links; the wake-up is a restart of about 2 s.

**Webhook test**: `webhook_test` sends one sample POST to the configured URL (`"uid":"00000000"`, `"reader":"test"`, `"test":true`, no card read) and returns at once; the outcome arrives as the `webhook` event. `BAD_ARGS` when no URL is set. Use it to check the URL, the token and TLS without a card.

**Hardware test**: `led_test` shows a fixed colour or one status look (the same patterns the status LED uses) on the onboard WS2812 (GPIO 27) and on the external RGB LED for `ms`, then returns to normal; `buzzer_test` sounds the buzzer. The onboard WS2812 also mirrors the status LED during normal operation.
**Wi-Fi setup** works over every link. `wifi_scan` is non-blocking: call it until `scanning` is `false` (a dual-band scan takes 10 to 30 s, and a reader joined to a network leaves it while scanning, because hopping across both bands drops the link, then rejoins it), then `networks` lists up to 20 networks, strongest first, one per SSID, with `band` `"2.4"` or `"5"` and `secure` false for open networks (the ESP32-C5 scans both bands). `wifi_connect` (`pass` empty for an open network, otherwise 8 to 63 characters) answers at once; poll `wifi_status` for `state` `connecting`, `connected` or `failed` (`error`: `Wrong password`, `Network not found` or `Could not connect`). On success the network is saved and the setup hotspot closes; on failure the reader resets its Wi-Fi driver and returns to its previous network (`state` stays `failed` with the `error` until the next attempt). Switching network over Wi-Fi drops that connection, so reconnect to the new address (or use USB or Bluetooth). In setup mode (no network saved, or after the BOOT forget-Wi-Fi sequence) the reader is only reachable at `192.168.4.1` on its own hotspot; `gapura.local` exists only once it has joined your network. The setup hotspot page also lists networks through `GET /setup/scan` (same shape, hotspot only).

**Card security** (`security:{secret, factoryKey, enabled}` in `config_set`: `secret` is 32 hex chars = 16 bytes, empty = forget it; `factoryKey` is key A of blank cards, 12 hex, default `FFFFFFFFFFFF`, and can be overridden per `card_write_credential`; `enabled` (default true) switches card security on or off while keeping the secret; `config_get` returns `security:{set, id, enabled, factoryKey}` with `id` a short fingerprint to compare readers and `factoryKey` masked). One secret is shared by all readers and the server and never stored on a card. From it and the card UID: `keyA = HMAC-SHA256(secret, "gapura-keyA" || UID)[0..6]`, `keyB` likewise with `"gapura-keyB"`. **Credential** (the user text that the server compares with its database): `card_write_credential` stores up to 95 characters, zero padded to 96 bytes, in blocks 4, 5, 6 and 8, 9, 10; block 12 holds `mac = HMAC-SHA256(secret, "gapura-cred" || UID || those 96 bytes)[0..16]`; blocks 7, 11 and 15 are the sector trailers (keyA, access bits `78 77 88 69`, keyB): data readable with A or B, writable only with B, keys never readable. Blank sectors (factory key) are keyed on the first write (`keyed: true`); a locked card is rewritten with key B and the data sectors are written before the signature. `card_read_credential` returns `BAD_SIGNATURE` for a card not written with this secret and `AUTH_FAILED` if no credential was written with it. `card_reset` wipes the credential and sets `factoryKey` as key A and B with the factory access bits (`FF 07 80 69`), so the card is blank again; it needs the secret that locked the card: this reader's, or the one passed as `secret` for a card locked elsewhere (`AUTH_FAILED` otherwise). A card whose secret is lost cannot be reset by the reader. With a secret set and `enabled`, the webhook body is `credential` (the text, or `null` when absent or its signature is invalid), `verified` (`true` when the credential is valid) and `text: null`; keyboard mode types the credential (or the UID when `keyboard.source` is `uid`) only for a valid card. With security off, `text` is the plain block from the `keyboard.*` settings and `credential`/`verified` are `null`. Keep the credential an opaque identifier (for example an employee or student number) and look the person up in the database; do not store personal data on the card. A full copy of a card (UID and data) still passes, so the server must watch for duplicate use.

**Reader mode** (`reader:{mode}` in `config_set`, applied after reboot): `rc522` (default), `pn532`, `rdm6300`, `rc522+rdm6300` or `pn532+rdm6300`; wiring in [`docs/wiring/`](wiring/). `info.reader` reports the running mode, `hf` (`"RC522"`, `"PN532"` or `null`), `hfOk` (that chip answers) and `lf` (`"RDM6300"` or `null`); `info.rc522` stays as the `hfOk` value for older clients. Every `tag` carries `reader` (`rc522`, `pn532` or `rdm6300`). 125 kHz tags have `type: "EM4100"` and a 5-byte `uid` (version byte + 32-bit id, e.g. `0F 00 A1 B2 C3`); the RDM6300 only reads the UID, so `read_uid` returns it but block and card commands work on 13.56 MHz cards only and answer `NO_READER` in `rdm6300` mode. Combined modes alternate 300 ms (13.56 MHz) and 400 ms (125 kHz) slots with one antenna on at a time; a MIFARE command takes the 13.56 MHz slot at once.

**Webhook** (fixed readers, no phone): `webhook:{url, token}` in `config_set` (`url` empty = off, `http://` or `https://`, token masked in `config_get`). On every `tag` the device POSTs `Content-Type: application/json` with `Authorization: Bearer <token>` and the body `{ "device": "Gapura-AAFF", "uid": "04A1B2C3", "reader": "rc522", "text": "123456789012345" | null, "ts": 386399 }`. `uid` is hex without spaces; `reader` is the reader that saw the tag; for a 125 kHz tag only `uid` is filled (`text`, `credential` null, `verified` false with security on); `text` is the card ID read from the `keyboard.block` / `key` / `keyType` settings (null if unreadable); `ts` is device uptime in ms. HTTPS is validated against the built-in CA bundle. `info.webhook` is the last HTTP status (0 = none yet, negative = transport error). The server must decide access; the device keeps no allow-list.

**Keyboard mode** (`mode: "keyboard"`): over BLE the device is a HID keyboard instead of the JSON service. After pairing with the passkey it types the card text (the `source` block up to the first `0x00`, or the UID without spaces) and Enter on every `tag`. Wi-Fi and USB keep working with the JSON protocol, so switch back with `config_set {"mode":"app"}` there. `token` is the device password (factory default `rfid1234`, `info.defaultPassword` is `true` while unchanged); `ble.passkey` defaults to `123456`. `config_get` masks `wifi.pass` and `token`. Wi-Fi and BLE passkey changes take effect after `reboot`. A 125 kHz tag types its UID, and only while card security is off.

Error codes: `NO_CARD`, `AUTH_FAILED`, `READ_FAILED`, `WRITE_FAILED`, `FORBIDDEN_BLOCK`, `BAD_ARGS`, `UNAUTHORIZED`, `BUSY`, `UNKNOWN_CMD`, `LOW_BATTERY`, `UNSUPPORTED_CARD`, `BAD_SIGNATURE`, `NO_READER` (no 13.56 MHz reader in this reader mode). Invalid JSON is answered with `id: null` and `BAD_ARGS`.

`write_block` / `write_text` refuse block 0 and sector trailers (3, 7, 11, …) with `FORBIDDEN_BLOCK`, and refuse writes with `LOW_BATTERY` when the battery is critical.

## 3. Events

| event | when |
|---|---|
| `boot` | once after reset; `reader` is the reader mode; `ok: false` means the 13.56 MHz chip did not answer (`rc522` is its version: RC522 `VersionReg`, `0x00`/`0xFF` when missing, or the PN532 firmware, `0x16` = v1.6; the RDM6300 cannot be probed) (`ina219` is informational: the sensor is optional) |
| `tag` | card detected (debounced 1 s per UID) while scanning |
| `tag_removed` | card left the field |
| `battery` | every 5 s, and immediately on `state` / low / critical change |
| `webhook` | after each webhook POST: `{ status, ms, body }`; `status` is the HTTP code or a negative transport error, `ms` the round trip, `body` the first ~100 characters of the reply (control characters replaced by spaces) |
| `sleep` | `{ reason, pct? }` before deep sleep: `battery_critical` (critical for 90 s while not charging; wakes on a 300 s timer and re-checks), `button` (BOOT held 2 s then released) or `command` (the `sleep` command); the last two wake only with the RESET (EN) button |

## 4. Links

| Link | In | Out |
|---|---|---|
| USB serial 115200 | one JSON line ending in `\n` | one JSON line per response/event |
| WebSocket `ws://gapura.local/ws?token=…` | text frame | text frame |
| HTTP `POST /api/cmd`, header `Authorization: Bearer <token>` | body = request | body = response (no events; use WS). `401` bad token, `503` queue full (`BUSY`) |
| BLE | write to `CMD` | notify on `RESP` (responses + events), chunked ≤ 180 B, message ends in `\n` |

OTA: `POST /api/ota` (multipart, Bearer token) with `firmware.bin`, or `littlefs.bin` with `?target=fs`. Replies `{"ok":true}` and restarts; `401` bad token, `500` write failed.

BLE requires pairing with the 6-digit passkey (`config_get` → `ble.passkey`); `CMD` needs an encrypted link and events are only notified to a paired client.

Wi-Fi setup: with no SSID stored, or if the first connection fails for 20 s, the device opens the WPA2 hotspot `Gapura-Setup-XXXX` (password `rfid1234`); `http://192.168.4.1/setup` saves SSID/password and restarts. Sign in to the dashboard with the device password and change it in Settings.

BLE UUIDs (frozen after Phase 5): service `6e400001-b5a3-f393-e0a9-e50e24dcca9e`, `CMD` `6e400002-…`, `RESP` `6e400003-…`.
