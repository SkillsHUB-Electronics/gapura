# Gapura: Rencana Implementasi

> 🇬🇧 English version: [PLAN.md](PLAN.md)

Pembaca/penulis RFID ESP32 + MFRC522 (RC522) bertenaga baterai, dapat diakses lewat **Wi-Fi (LAN)**, **Bluetooth Low Energy**, dan **USB**, dilengkapi LED RGB sebagai indikator status, pemantauan baterai INA219, dan charger Li-ion dengan deteksi status pengisian.

Status (2026-10-08): **fase 0–7 sudah diimplementasikan; fitur inti sudah diuji di perangkat.** Sisa pekerjaan ada di [4.1 Sisa pekerjaan](#41-sisa-pekerjaan). Setiap fase di bawah = satu branch dan satu pull request. 🔀 menandai pekerjaan yang bisa dikerjakan paralel.

---

## 1. Komponen

### 1.1 Hardware

| # | Komponen | Fungsi | Antarmuka |
|---|---|---|---|
| 1 | Waveshare ESP32-C5-WIFI6-KIT (ESP32-C5-WROOM-1, 16 MB flash, 8 MB PSRAM) | MCU, dual-band Wi-Fi 6, BLE 5, USB-serial (CH343) | – |
| 2 | MFRC522 (RC522) 13,56 MHz | Baca/tulis MIFARE Classic 1K, Ultralight/NTAG | SPI (VSPI) |
| 3 | LED RGB **common anode**, 3 × 220 Ω | Indikator status perangkat (anoda → 3V3, katoda → GPIO, PWM active-low) | 3 × PWM LEDC |
| 4 | Modul INA219 (shunt 0,1 Ω, alamat `0x40`), **opsional** | Arus dan daya baterai (tegangan juga dari ADC board) | I²C |
| 5 | Charger ETA6098 onboard + konektor baterai MX1.25 (di board Waveshare) | Mengisi sel Li-ion 1S dari USB; hanya LED STAT, tanpa GPIO status | BAT_ADC → GPIO 6 |
| 6 | Li-ion 18650 1S 3,7 V, **2000 mAh** | Sumber daya | – |
| 7 | Buck MP1605 onboard (3,3 V, 2 A, input 2,3–5,5 V) | Catu sistem dari USB atau baterai (tanpa konverter eksternal) | – |
| 8 | Buzzer aktif (opsional) | Umpan balik suara saat scan | GPIO |
| 9 | Pembagi baterai 200 kΩ / 100 kΩ (onboard) | Pembacaan tegangan baterai, VBAT / 3 | ADC |
| 10 | PN532 (model Elechouse V3), **pengganti RC522** | 13,56 MHz seperti RC522 (MIFARE Classic, pengamanan kartu), jangkauan lebih jauh | SPI, pin sama |
| 11 | RDM6300 125 kHz (mis. WANGCL), **opsional** | Tag EM4100, hanya UID (read-only) | UART RX, 9600 baud |
| 12 | Boost MT3608 (5,0 V), pembagi 1 kΩ / 2 kΩ, P-MOSFET AO3401 + NPN BC547 | 5 V untuk RDM6300 saat pakai baterai, TX-nya diturunkan ke 3,3 V, dan saklar dayanya di mode kombinasi | – |

Terkonfirmasi: LED RGB common anode, 18650 2000 mAh, lisensi MIT.

### 1.2 Peta pin

| Fungsi | GPIO ESP32-C5 | Catatan |
|---|---|---|
| RC522 SS / SDA | 23 | |
| RC522 SCK | 10 | FSPI default board |
| RC522 MOSI | 8 | FSPI default board |
| RC522 MISO | 9 | FSPI default board |
| RC522 RST | 24 | |
| RC522 VCC | 3V3 | **jangan 5 V** |
| I²C SDA (INA219) | 0 | default board |
| I²C SCL (INA219) | 1 | default board |
| RGB – R / G / B (katoda) | 2 / 3 / 7 | LEDC, 5 kHz, 8-bit, terbalik (duty 255 = mati); pin strapping, baru dikendalikan setelah boot |
| Buzzer | 25 | pin strapping; lewat transistor, jangan ada pull-down di pin |
| Tegangan baterai (BAT_ADC) | 6 (ADC1) | pembagi onboard 200 kΩ/100 kΩ (VBAT / 3), lewat R39 0 Ω |

| PN532 SCK / MISO / MOSI / SS | 10 / 9 / 8 / 23 | pengganti RC522, bus sama; mode SPI (DIP 1 OFF, 2 ON), VCC 3V3, tanpa jalur reset |
| RDM6300 TX → UART1 RX | 4 | lewat R1 1 kΩ, dengan R2 2 kΩ ke GND (5 V → 3,3 V) |
| Saklar daya RDM6300 | 5 | HIGH = nyala: R3 1 kΩ ke basis BC547 (R5 100 kΩ ke GND), kolektor menarik gate AO3401 (R4 10 kΩ ke 5 V) |

Terpakai di Waveshare ESP32-C5-WIFI6-KIT: 11/12 UART0 (CH343, port COM), 13/14 USB native, 15 PSRAM, 16–22 flash, 27 RGB onboard, 28 tombol BOOT.

### 1.2a Mode reader

`reader.mode` di Settings dashboard (Card reader), berlaku setelah restart. Wiring tiap mode: [`docs/wiring/`](wiring/).

| Mode | Terpasang | Fungsi | Wiring |
|---|---|---|---|
| `rc522` (default) | RC522 | UID, blok, pengamanan kartu | [rc522.svg](wiring/rc522.svg) |
| `pn532` | PN532 | sama dengan RC522 | [pn532.svg](wiring/pn532.svg) |
| `rdm6300` | RDM6300 | hanya UID 125 kHz; perintah MIFARE menjawab `NO_READER` | [rdm6300.svg](wiring/rdm6300.svg) |
| `rc522+rdm6300` | RC522 + RDM6300 | keduanya; fitur 13,56 MHz untuk kartu 13,56 MHz | [rc522-rdm6300.svg](wiring/rc522-rdm6300.svg) |
| `pn532+rdm6300` | PN532 + RDM6300 | keduanya; fitur 13,56 MHz untuk kartu 13,56 MHz | [pn532-rdm6300.svg](wiring/pn532-rdm6300.svg) |

Mode kombinasi bergantian agar hanya satu antena yang aktif: 300 ms 13,56 MHz (antena nyala, RDM6300 tanpa daya), lalu 400 ms 125 kHz (antena mati, RDM6300 dinyalakan lewat GPIO 5). Tag 125 kHz dianggap diangkat setelah satu slot tanpa frame (500 ms tanpa frame jika RDM6300 sendirian). Perintah MIFARE langsung pindah ke slot 13,56 MHz. RDM6300 butuh 5 V: boost MT3608 dari 3V3 (pin 5V board hanya hidup dari USB), dan TX 5 V-nya diturunkan ke 3,3 V. Tag 125 kHz tidak punya credential: webhook dan keyboard mengirim UID-nya (keyboard hanya saat pengamanan kartu mati). RC522 + PN532 bersamaan tidak disediakan (bus dan chip select sama).

### 1.3 Jalur daya

```
USB 5V ──► ETA6098 (onboard) ──► BAT ──► sel Li-ion (+) di konektor MX1.25 (pin 1 = BAT, pin 2 = GND)
                                  BAT ──► catu board (3V3)
                                  BAT ──► pembagi 200k/100k ──► GPIO 6 (BAT_ADC)
```

Charger onboard **tidak punya pin status** (hanya LED STAT). Tegangan baterai dibaca di GPIO 6 (×3). INA219 opsional: pasang **hanya di cabang sel baterai** (antara keluaran charger dan sel) dan **tanda arus menunjukkan arah**: positif = discharge, negatif = charging.

### 1.4 Logika status pengisian

| Kondisi | Status yang dilaporkan |
|---|---|
| Tegangan < 2,5 V (tanpa sel) | `fault` |
| Dengan INA219: arus < −20 mA | `charging` |
| Dengan INA219: arus > +20 mA | `discharging` |
| Tanpa INA219: tegangan naik > 20 mV dalam 45 dtk | `charging` |
| Tanpa INA219: tegangan turun > 10 mV dalam 45 dtk | `discharging` |
| Charging dan ≥ 4,15 V, datar | `full` |

Tanpa INA219, status adalah **estimasi** dari tren tegangan (lonjakan beban bisa mengecohnya).

Persentase baterai (SoC) = tabel kurva tegangan Li-ion (tegangan istirahat), dihaluskan dengan coulomb counting dari arus INA219. `low` < 15 %, `critical` < 5 % (perangkat masuk deep sleep, RFID dimatikan).

### 1.5 Status RGB (prioritas tertinggi menang)

| Prioritas | Kondisi | Warna / pola |
|---|---|---|
| 1 | Baterai kritis | merah, kedip cepat |
| 2 | Error (perintah gagal, RC522 tidak terdeteksi) | merah, 1 kilat |
| 3 | Baca/tulis tag berhasil | hijau / biru, 1 kilat |
| 4 | AP setup Wi-Fi aktif | ungu, kedip lambat |
| 5 | Menyambung Wi-Fi | biru, kedip lambat |
| 6 | Sedang charging | amber, breathing |
| 7 | Baterai lemah | oranye, kedip lambat |
| 8 | Client terhubung (WS/BLE/USB) | cyan, redup menyala |
| 9 | Idle, siap | hijau, breathing redup |

Kecerahan dapat diatur (`led.brightness`, 0–100). Pola tidak memblokir (berbasis `millis()`). Common anode: LED menyala saat GPIO LOW, jadi PWM dibalik dan semua pin di-set HIGH saat boot agar tidak berkedip.

---

## 2. Arsitektur software

### 2.1 Modul firmware

| Modul | Tanggung jawab |
|---|---|
| `rfid` | Mode reader dan slot waktu, polling kartu (100 ms), debounce, baca/tulis blok, dump, pengamanan kartu; menolak tulis blok 0 dan sector trailer |
| `hf` | Antarmuka chip 13,56 MHz: `Rc522Reader` (`hf_rc522`), `Pn532Reader` (`hf_pn532`, framing SPI sendiri) |
| `rdm6300` | Frame UART RDM6300 (EM4100), saklar daya |
| `power` | Sampling INA219 (1 Hz), pin charger, state machine, SoC %, ambang low/critical |
| `led` | State machine RGB dengan prioritas dan pola non-blocking |
| `router` | Parse request JSON, dispatch perintah, format response; event bus yang menyebarkan event ke semua jalur |
| `store` | Konfigurasi permanen di NVS (Wi-Fi, token, nama perangkat, LED, beep, ambang baterai) |
| `net` | Wi-Fi STA + fallback AP setup, mDNS `gapura.local`, REST `POST /api/cmd`, WebSocket `/ws`, dashboard statis dari LittleFS |
| `ble` | NimBLE, service gaya Nordic UART, chunking dan penyusunan ulang |
| `serial_link` | USB serial 115200, NDJSON |

### 2.2 Alur data

```
             ┌──────────────────────────── ESP32 ────────────────────────────┐
 RC522 ─SPI─►│ rfid ──tag──┐                                                 │
 INA219 ─I²C►│ power ─batt─┼─► event bus ─┬─► net (WS)  ───────────────────► │──► Browser / HP (LAN)
 BAT_ADC ───►│             │              ├─► ble (notify) ────────────────► │──► HP / PC (BLE)
             │             │              └─► serial_link ─────────────────► │──► PC (USB)
             │             └─► led (RGB)                                     │
             │                                                               │
             │  net / ble / serial_link ──request──► router ──► rfid/power/store
             │                                  ◄──response──                │
             └───────────────────────────────────────────────────────────────┘
```

1. **Perintah:** client → jalur mana pun → `router` → modul → response kembali lewat jalur **yang sama**.
2. **Event:** `rfid` (tag terdeteksi/dilepas) atau `power` (sampel baterai, perubahan status charge) → event bus → **semua** jalur yang terhubung + `led`.
3. **Status:** error dari `net`, `ble`, `router` juga dikirim ke `led`.
4. Hanya satu operasi RFID berjalan dalam satu waktu (mutex); request bersamaan dibalas `BUSY`.

### 2.3 Protokol

Satu protokol JSON untuk semua jalur. Kontrak: [PROTOCOL.md](PROTOCOL.md). Tambahan di revisi ini:

- Perintah `battery` → `{ v, mA, mW, pct, state }`
- `info` kini memuat `battery`
- Event `battery` tiap 5 detik selama ada client terhubung, dan langsung saat status charge berubah
- Kunci konfigurasi `led.brightness`, `battery.lowPct`, `battery.capacityMah` (default 2000)

### 2.4 Dashboard

Vite + TypeScript tanpa framework UI (bundle kecil untuk LittleFS). Tampilan bersih dan modern:

- Font sistem (jalan offline, tanpa CDN), tema terang/gelap mengikuti `prefers-color-scheme`
- Layout berbasis kartu, grid spasi 8 px, satu warna aksen, sudut membulat, bayangan halus
- Top bar tetap: chip koneksi (Wi-Fi / BLE / USB), **chip baterai** (%, ikon petir saat charging, warna sesuai level), cermin status LED
- Tampilan: **Connect**, **Scan** (feed tag live), **Card** (baca/tulis blok, grid dump 16 × 4, peringatan trailer), **Power** (tegangan, arus, SoC, status charge, sparkline 10 menit terakhir), **Settings**
- Mobile-first, bisa di-install sebagai PWA

---

## 3. Daftar file (struktur ringkas)

```
gapura/
├─ CLAUDE.md                 konteks untuk AI assistant & kontributor (EN + ID)
├─ README.md / README.id.md  gambaran proyek, quick start
├─ LICENSE                   MIT
├─ docs/
│  ├─ PLAN.md / PLAN.id.md   rencana ini
│  ├─ PROTOCOL.md            kontrak JSON bersama firmware & dashboard
│  ├─ wiring/                wiring tiap mode reader (SVG, dari tools/wiring.py)
│  └─ hardware/              foto, skematik (Fase 0)
├─ firmware/                 PlatformIO, Arduino framework
│  ├─ platformio.ini
│  ├─ src/
│  │  ├─ main.cpp            setup/loop, menyambungkan modul
│  │  ├─ config.h            pin, konstanta, FW_VERSION
│  │  ├─ rfid.h/.cpp        mode reader, MIFARE, pengamanan kartu
│  │  ├─ hf.h, hf_rc522.cpp, hf_pn532.cpp
│  │  ├─ rdm6300.h/.cpp
│  │  ├─ power.h/.cpp
│  │  ├─ led.h/.cpp
│  │  ├─ router.h/.cpp
│  │  ├─ commands.h/.cpp    perintah protokol → modul
│  │  ├─ util.h            helper murni: kurva SoC, aturan MIFARE, hex
│  │  ├─ store.h/.cpp
│  │  ├─ net.h/.cpp
│  │  ├─ ble.h/.cpp
│  │  └─ serial_link.h/.cpp
│  ├─ data/                  hasil build dashboard (LittleFS, di-ignore git)
│  └─ test/test_router/      unit test native (tanpa hardware)
├─ dashboard/                Vite + TypeScript
│  ├─ index.html
│  ├─ package.json, vite.config.ts, tsconfig.json
│  └─ src/
│     ├─ main.ts             app shell & routing
│     ├─ device.ts           client: pencocokan id, event, state
│     ├─ ui.ts               helper DOM, ikon, toast
│     ├─ protocol.ts         tipe request/response/event
│     ├─ transport/          ws.ts, ble.ts, serial.ts, mock.ts
│     ├─ views/              connect, scan, card, power, settings
│     └─ style.css           design token, terang/gelap
└─ tools/
   ├─ rfid_cli.py            client uji lewat USB / Wi-Fi
   └─ wiring.py              membuat docs/wiring/*.svg
```

---

## 4. Fase

| Fase | Cakupan | Selesai jika |
|---|---|---|
| **0 Persiapan** | Repo, lisensi, `.gitignore`, wiring di breadboard, foto di `docs/hardware/` | `pio run` berhasil build sketch kosong dan flash |
| **1 RFID via USB** | `config.h`, `rfid`, event UID di serial | Tempel kartu → muncul `{"event":"tag",...}`; versi RC522 0x91/0x92 |
| **2 Power + LED** 🔀 | `power` (INA219 + pin charger + SoC), state machine `led` | Serial menampilkan V/mA benar; cabut USB → `charging` berubah ke `discharging`; LED sesuai tabel prioritas |
| **3 Router + USB** | `router`, `serial_link`, semua perintah termasuk `battery`; `tools/rfid_cli.py` 🔀; test native 🔀 | Semua perintah di PROTOCOL.md jalan dari CLI; tulis lalu baca blok 4 cocok |
| **4 Wi-Fi** | `store`, `net` (STA/AP, mDNS, REST, WS, token) | `curl` ke `gapura.local` berhasil; client WS menerima event `tag` dan `battery` |
| **5 BLE** | `ble` (NimBLE, chunking, passkey) | nRF Connect bisa kirim `ping` dan menerima event; free heap dicatat saat Wi-Fi + BLE aktif |
| **6 Dashboard** 🔀 (bisa mulai sejak Fase 3 dengan `mock.ts`) | Semua tampilan, chip baterai, tampilan Power, PWA, build → `firmware/data/` | Tersaji dari `gapura.local`; dev lokal terhubung via Web Serial dan Web Bluetooth |
| **7 Penguatan** | Mutex, watchdog, reconnect Wi-Fi, OTA (dilindungi token), deep sleep saat baterai kritis, CI | CI hijau; uji stres 30 menit (scan + WS + BLE) tanpa reboot; durasi baterai terukur |
| **8 Opsional** | ~~Keyboard USB HID (ESP32-S3)~~ selesai sebagai mode keyboard BLE HID; ~~webhook~~ selesai (termasuk HTTPS); MQTT, app Flutter, backend riwayat scan | – |

### 4.1 Sisa pekerjaan

Diperbarui 2026-10-08, urut prioritas.

**Sudah dikode, belum diuji di hardware**

1. Webhook HTTPS ke server perusahaan (baru diuji ke httpbin.org).
2. Tap kartu nyata dengan webhook dan mode keyboard BLE aktif bersamaan.
3. Wi-Fi putus dua kali tepat setelah client WebSocket dashboard tersambung (uji stres 30 menit); penyebab belum ditemukan.
4. Tes pemalsuan `BAD_SIGNATURE` dengan secret produksi.
5. Peringatan baterai rendah, deep sleep, dan bangun lewat timer 300 dtk.
6. PN532, RDM6300 dan mode kombinasinya; INA219; wiring LED RGB dan buzzer eksternal.
7. Hotspot setup `Gapura-Setup-XXXX` setelah ganti nama (yang diuji ulang baru Wi-Fi STA).

**Belum dikerjakan**

8. Kalibrasi tegangan baterai GPIO 6 dengan multimeter.
9. Mengukur durasi baterai (syarat selesai Fase 7).
10. Redesain dashboard (referensi desain UI belum diterima).
11. Foto dan skematik di `docs/hardware/` (Fase 0); foldernya belum ada.

**Opsional / ditunda (Fase 8)**

12. MQTT sebagai kanal tambahan (perintah, status, OTA) di samping webhook, setelah firmware dan dashboard rapi.
13. App Flutter.
14. Backend riwayat scan.

---

## 5. Risiko

- **RAM:** Wi-Fi + BLE + web server berat → NimBLE, partisi `huge_app`, pantau `info.heap`.
- **Kartu terkunci:** menulis sector trailer bisa mengunci kartu permanen → diblokir secara default.
- **Brown-out:** RC522 + TX Wi-Fi puncak ~300 mA; sel lemah atau boost kecil menyebabkan reset → pakai buck-boost ≥ 1 A dan kapasitor 470 µF dekat ESP32.
- **Sel tanpa proteksi:** pakai sel atau pack dengan rangkaian proteksi (charger onboard tidak memutus beban saat tegangan rendah).
- **Polaritas MX1.25:** pin 1 = BAT (+), pin 2 = GND; cocokkan dengan sablon board sebelum memasang pack.
- **Tidak ada pin bangun dari charger:** deep sleep bangun dengan timer (300 dtk) lalu mengecek baterai.
- **Web Bluetooth** tidak ada di Safari iOS; **Web Serial** hanya di Chrome/Edge desktop.
- **Password default pabrik** (`rfid1234`, passkey BLE `123456`) terbuka di repo → dashboard menampilkan peringatan sampai diganti di Settings.
- **MIFARE Classic Crypto1 tidak aman** → jangan untuk pembayaran atau akses keamanan tinggi.

---

## 6. Pertanyaan terbuka

1. Referensi desain UI (disebut terlampir, belum diterima).
2. Kalibrasi tegangan baterai pembagi GPIO 6 terhadap multimeter.
