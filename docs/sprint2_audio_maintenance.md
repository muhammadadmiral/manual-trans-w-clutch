# Sprint 2: Audio, Fuel, dan Maintenance

Sprint 2 tetap berada dalam satu ASI supaya audio, drivetrain, fuel, dan wear
membaca state frame yang sama. Implementasi tidak memakai FMOD atau DLL pihak
ketiga. Mixer menggunakan XAudio2 bawaan Windows SDK; visual memakai native
GTA, jadi bundle runtime cukup berisi ASI dan WAV.

## Audio

`src/Audio/AudioEngine` memuat PCM WAV stereo 44.1 kHz 16-bit dari:

```text
melar-transmission/audio/
```

Audio mekanikal bersifat event-based. Mesin, exhaust, road noise, tyre slip,
turbo spool, angin, tunnel, dan occlusion tetap dirender oleh GTA. Mod hanya
menambahkan:

- shift mobil normal, slow/soft, dan harsh/power;
- shift motor up/down normal dan soft;
- missed shift motor;
- parking brake apply/release;
- bunyi tuas automatic saat selector berpindah P-R-N-D-S-L.

Setiap bank memilih varian secara acak tanpa mengulang file terakhir jika ada
lebih dari satu varian. Pitch dan level mendapat variasi kecil. Saat load,
peak setiap WAV dibatasi ke `LimiterCeiling`; mastering voice menyisakan
headroom tambahan agar beberapa event yang berdekatan tidak clipping.

Perpindahan internal D1-D2-D3 tidak memakai bunyi selector. Shift D yang ringan
memilih bank slow, shift biasa memilih bank normal, sedangkan kickdown,
limiter-pressure, power shift, dan quickshifter memilih karakter harsh.

Nama file bundle adalah kontrak runtime. Bank sudah menyediakan slot varian
berakhiran `01` sampai `04` atau `06`; file yang tidak ada dilewati tanpa
error. Daftar lengkap dan panduan menyiapkan sampel ada di
`docs/audio_asset_reference.md`.

`NativeLayers=1` menghidupkan layer boost native sesuai spool turbo, blow-off
native ketika throttle ditutup setelah boost, serta exhaust-pop singkat pada
harsh shift. Tyre slip, road surface, tunnel, dan wind tetap mengikuti fisika
dan audio engine GTA agar tidak terdengar ganda.

## Refuel

`RefuelInteraction` melacak kendaraan terakhir yang dikemudikan. Syarat isi:

1. pemain sudah turun dan berada maksimal 5 meter dari kendaraan;
2. mesin mati;
3. ada prop pom bensin yang dikenali dalam radius, atau pemain membawa
   `WEAPON_PETROLCAN`;
4. tombol `Refuel` ditahan.

Pom terdekat tetap dideteksi dari model native. Daftar koordinat hanya dipakai
untuk blip short-range di minimap dan dapat dimatikan lewat `FuelBlips`.
Jalur pom
memuat `prop_cs_fuel_nozle`, memasangnya ke bone tangan kanan, memutar animasi,
dan mengisi tank berdasarkan delta-time. Jalur darurat memakai jerigen GTA.
Masuk kendaraan, melepas tombol, mati, menjauh, atau menyalakan mesin selalu
membersihkan task dan prop.

Fuel disimpan per identitas model + nomor pelat selama sesi, sehingga turun
untuk mengisi bensin tidak mereset tangki.

## Oil maintenance

Oil life memakai jarak, jam mesin, load, cold operation, dan temperatur tinggi.
Di bawah ambang service, output power turun bertahap paling jauh 12%; tidak ada
write handling permanen.

Untuk ganti oli:

1. parkir dan matikan mesin;
2. turun lalu berdiri dekat sisi depan/kap;
3. tahan tombol `Oil Service` selama proses;
4. animasi mechanic native diputar dan progress tampil di HUD.

Melepas tombol atau menjauh membatalkan service dengan aman.

## HUD dan kompatibilitas

Renderer tetap native GTA. Tidak ada SVG/Chromium/Scaleform kustom karena itu
menambah packaging, input focus, dan failure mode yang tidak dibutuhkan untuk
HUD data sederhana.

- gear HUD, pedal/simulation bars, dan menu punya posisi terpisah;
- semua panel dijepit ke safe-zone GTA dan menyesuaikan aspect ratio;
- gear HUD default berada di kanan atas agar tidak menabrak minimap atau
  speedometer Menyoo yang umumnya berada di bawah;
- X/Y dan scale dapat diubah live dari `HUD Settings`;
- setiap widget utama dapat dimatikan jika speedometer lain sudah menampilkan
  data yang sama;
- prompt fuel/service memakai satu slot prioritas dan animasi masuk, sehingga
  prompt oli tidak menimpa prompt bensin.

## Bundle

Isi `bundle/` disalin ke root GTA V:

```text
melar-transmission.asi
melar-transmission/
  audio/
    *.wav
```

Runtime hanya membaca struktur bundle `melar-transmission/audio` yang seragam.
