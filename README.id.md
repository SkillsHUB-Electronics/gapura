# Gapura

*Pembaca/penulis RFID nirkabel: ESP32-C5 + RC522 lewat Wi-Fi, Bluetooth LE, dan USB.*

> 🇬🇧 English: [README.md](README.md)

Pembaca/penulis RFID **ESP32 + RC522** open source bertenaga baterai yang bisa dipakai HP atau PC mana pun lewat **Wi-Fi**, **Bluetooth LE**, atau **USB**, dengan satu protokol JSON dan dashboard web bawaan.

**Status:** firmware dan dashboard sudah berjalan di hardware (Waveshare ESP32-C5). Lihat [docs/PLAN.id.md](docs/PLAN.id.md) dan [CLAUDE.md](CLAUDE.md).

## Fitur

- Baca/tulis MIFARE Classic 1K dan NTAG/Ultralight (13,56 MHz)
- Tiga jalur, satu protokol: Wi-Fi (REST + WebSocket, `http://gapura.local`), BLE (Web Bluetooth / mobile), USB serial (Web Serial / script)
- Pemantauan baterai dengan INA219 (tegangan, arus, %), deteksi status pengisian (charging / full / discharging)
- LED RGB sebagai indikator status
- Dashboard web bersih dan mobile-first, disajikan langsung dari perangkat
- Penulisan aman: blok 0 dan sector trailer dilindungi
- Update over-the-air (firmware dan dashboard), watchdog, deep sleep saat baterai kritis

## Hardware

Waveshare ESP32-C5-WIFI6-KIT · RC522 atau PN532 (13,56 MHz) dan/atau RDM6300 (125 kHz) · INA219 opsional · charger onboard · sel 18650 2000 mAh (konektor MX1.25) · LED RGB common anode · buzzer opsional.
Peta pin dan jalur daya: [docs/PLAN.id.md §1](docs/PLAN.id.md#1-komponen). Wiring tiap mode reader (RC522, PN532, RDM6300, RC522 + RDM6300, PN532 + RDM6300): [docs/wiring/](docs/wiring/); pilih mode di dashboard, Settings → Card reader.

## Struktur repository

```
docs/        rencana, protokol, hardware
firmware/    firmware ESP32 (PlatformIO, Arduino)
dashboard/   dashboard web (Vite + TypeScript)
tools/       client uji (Python)
```

## Mulai cepat

Prasyarat: VS Code + PlatformIO, Node.js 20+, Python 3.10+, Chrome/Edge (Web Serial & Web Bluetooth).

```bash
# firmware
cd firmware && pio run -t upload && pio device monitor
# dashboard
cd dashboard && npm ci && npm run dev      # buka http://localhost:5173/?demo untuk reader simulasi
npm run build && cd ../firmware && pio run -t uploadfs
```

Saat pertama menyala tanpa konfigurasi Wi-Fi, muncul hotspot `Gapura-Setup-XXXX` (password `rfid1234`) → buka `http://192.168.4.1`, isi SSID dan password Wi-Fi → buka `http://gapura.local` dan masuk dengan password default `rfid1234` (passkey Bluetooth `123456`). Ganti keduanya di **Settings**.

## Protokol

```jsonc
{ "id": 1, "cmd": "battery" }
{ "id": 1, "ok": true, "data": { "v": 3.92, "mA": 145, "pct": 78, "state": "discharging" } }
```

Kontrak lengkap: [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Berkontribusi

1. Ambil tugas dari `docs/PLAN.id.md` (satu item fase = satu issue), assign diri sendiri.
2. Branch `feat/<fase>-<topik>`, commit dengan [Conventional Commits](https://www.conventionalcommits.org/) (`feat(firmware): …`, `fix(dashboard): …`).
3. Buka PR ke `main`; CI harus lulus dan butuh satu review. Perubahan protokol butuh satu reviewer firmware **dan** satu reviewer dashboard.
4. Jangan commit rahasia asli (Wi-Fi Anda, password yang sudah diganti); hanya default pabrik yang ada di `config.h`. Pakai `firmware/src/secrets.h` (di-ignore git).
5. Tanpa hardware? Pakai `dashboard/src/transport/mock.ts` dan `pio test -e native`.

Gaya kode: C++ `clang-format` (Google, indent 2), loop non-blocking (`millis()`, tanpa `delay()` panjang); TypeScript strict + ESLint + Prettier; UI tidak pernah mengakses WebSocket/BLE/Serial langsung, hanya lewat interface `Transport`.

## Troubleshooting

| Gejala | Solusi |
|---|---|
| Instalasi framework ESP32-C5 gagal `FileNotFoundError` di `.platformio\.cache` (Windows) | Batas path yang sama di folder PlatformIO. Set juga core dir pendek: `$env:PLATFORMIO_CORE_DIR="C:\pio"` |
| Build gagal `No such file or directory` di dalam `NimBLE-Arduino` (Windows) | Path proyek terlalu panjang (batas 260 karakter). Build dengan workspace pendek: `set PLATFORMIO_WORKSPACE_DIR=C:\pio-ws\wireless-rfid` (PowerShell: `$env:PLATFORMIO_WORKSPACE_DIR="C:\pio-ws\wireless-rfid"`) |
| Event `boot` menampilkan `rc522: "0x00"` atau `"0xFF"` | Periksa wiring SPI atau catu 3,3 V RC522 |
| `gapura.local` tidak bisa dibuka | Sebagian Android tidak mendukung mDNS; pakai IP dari `info` |
| Web Bluetooth tidak ada di iPhone | Safari tidak mendukung; pakai Bluefy atau app native |

## Catatan keamanan

MIFARE Classic (Crypto1) tidak aman. Jangan gunakan perangkat ini untuk pembayaran atau kontrol akses keamanan tinggi.

## Lisensi

[MIT](LICENSE).
