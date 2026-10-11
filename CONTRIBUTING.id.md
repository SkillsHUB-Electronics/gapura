# Berkontribusi ke Gapura

> 🇬🇧 English: [CONTRIBUTING.md](CONTRIBUTING.md)

Terima kasih sudah mau membantu. Gapura terdiri dari hardware dan software, jadi kontribusi yang berguna termasuk laporan bug dengan log serial, foto wiring yang berhasil, pengujian di board atau reader lain, perbaikan dokumentasi, dan perubahan firmware atau dashboard.

Dengan ikut berkontribusi, Anda setuju mengikuti [Kode Etik](CODE_OF_CONDUCT.md).

## Sebelum mulai

- Baca [README](README.id.md), [docs/PLAN.id.md](docs/PLAN.id.md) (arsitektur dan pekerjaan tersisa), dan [docs/PROTOCOL.md](docs/PROTOCOL.md) (kontrak JSON).
- Cari dulu [issue](https://github.com/SkillsHUB-Electronics/gapura/issues) yang sudah ada. Untuk perubahan yang lebih besar dari perbaikan kecil, buka issue untuk menyepakati pendekatannya sebelum menulis kode.
- Pekerjaan yang masih terbuka ada di [docs/PLAN.id.md §4.1](docs/PLAN.id.md#41-sisa-pekerjaan). Satu item di sana adalah satu issue dan satu pull request.

## Menyiapkan lingkungan

Kebutuhan: VS Code + PlatformIO, Node.js 20+, Python 3.10+, Chrome atau Edge.

```bash
git clone https://github.com/<username-anda>/gapura.git
cd gapura

# Firmware: build, flash, unit test
cd firmware
pio run                    # build
pio run -t upload          # flash lewat USB
pio test -e native         # unit test di host (butuh gcc/g++)

# Dashboard
cd ../dashboard
npm ci
npm run dev                # http://localhost:5173/?demo memakai reader simulasi
npm run build              # cek tipe dan build ke firmware/data/
```

Di Windows, pakai path pendek: `set PLATFORMIO_CORE_DIR=C:\pio` dan `set PLATFORMIO_WORKSPACE_DIR=C:\pio-ws\gapura`.

Tidak punya hardware? Transport mock di dashboard (`dashboard/src/transport/mock.ts`, `?demo` di URL) dan `pio test -e native` sudah mencakup sebagian besar perubahan.

## Alur kerja

1. Fork repositori dan buat branch dari `main`: `feat/<topik>`, `fix/<topik>`, atau `docs/<topik>`.
2. Commit dengan format [Conventional Commits](https://www.conventionalcommits.org/): `feat(firmware): …`, `fix(dashboard): …`, `docs: …`.
3. Buat setiap pull request kecil dan fokus pada satu perubahan.
4. Buka pull request ke `main` dan isi templatenya. CI harus lulus dan butuh satu review maintainer.
5. Tambahkan satu baris di bagian `Unreleased` pada [CHANGELOG.md](CHANGELOG.md) jika perubahannya terasa oleh pengguna.

## Aturan proyek

Aturan ini menjaga firmware tetap stabil dan semua jalur tetap konsisten.

- **Protokol dulu.** Perintah baru atau yang diubah memperbarui `docs/PROTOCOL.md` dan `dashboard/src/protocol.ts` sebelum kode firmware atau UI. Perubahan protokol butuh satu review firmware dan satu review dashboard.
- **Firmware tidak blocking.** Tidak ada `delay()` panjang di `loop()`; pakai `millis()`. Pekerjaan RFID/SPI tetap di task loop utama.
- **Modul murni tetap murni.** `router` dan `util.h` tidak boleh meng-include header Arduino, supaya tetap jalan di `pio test -e native`.
- **Batas transport.** UI dashboard tidak pernah bicara langsung ke WebSocket, BLE, atau Serial, hanya lewat interface `Transport` dan `device.ts`.
- **Keamanan kartu.** Jangan izinkan penulisan ke blok 0 atau sector trailer di luar perintah khusus pengamanan kartu.
- **Dokumen dua bahasa.** Setiap dokumen punya file bahasa Inggris dan file `.id.md` bahasa Indonesia dengan struktur yang sama. Kode, nama identifier, dan pesan commit dalam bahasa Inggris.
- **Gaya kode.** C++: `clang-format` (gaya Google, indent 2). TypeScript: mode strict.

## Jangan pernah commit rahasia

Hanya nilai bawaan pabrik (`kDefaultPassword`, `kDefaultBlePasskey` di `firmware/src/config.h`) yang boleh ada di repositori. Simpan detail Wi-Fi dan kunci Anda sendiri di `firmware/src/secrets.h`, yang sudah di-ignore git. Jangan pernah commit backup pengamanan kartu, secret kartu, nomor kartu asli, data perusahaan, atau recovery code. Pakai UID dan ID karangan di contoh dan test.

## Melaporkan bug dan meminta fitur

Pakai [formulir issue](https://github.com/SkillsHUB-Electronics/gapura/issues/new/choose). Untuk bug, sertakan board, mode reader, versi firmware (perintah `info`), jalur yang dipakai (Wi-Fi, BLE, atau USB), dan log serial. Masalah keamanan dilaporkan lewat [SECURITY.md](SECURITY.md), bukan issue publik.

## Lisensi

Dengan berkontribusi, Anda setuju kontribusi Anda dilisensikan dengan [Lisensi MIT](LICENSE).
