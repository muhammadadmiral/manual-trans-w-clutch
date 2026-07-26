# Sprint 2: Audio, Fuel, dan Maintenance

Sprint 2 tetap berada dalam satu ASI supaya audio, drivetrain, fuel, dan wear
membaca state frame yang sama. Implementasi tidak memakai FMOD atau DLL pihak
ketiga. Mixer menggunakan XAudio2 bawaan Windows SDK; visual memakai native
GTA, jadi bundle runtime cukup berisi ASI dan WAV.

## Audio

`src/Audio/AudioEngine` memuat PCM WAV stereo 44.1 kHz 16-bit dari:

```text
manual-trans/audio/
```

Audio mekanikal bersifat event-based. Mesin, exhaust, road noise, tyre slip,
turbo spool, angin, tunnel, dan occlusion tetap dirender oleh GTA. Mod hanya
menambahkan:

- shift mobil normal, soft, dan power;
- shift motor up/down normal dan soft;
- missed shift motor;
- parking brake apply/release;
- automatic selector Park.

Setiap bank memilih varian secara acak tanpa mengulang file terakhir jika ada
lebih dari satu varian. Pitch dan level mendapat variasi kecil. Saat load,
peak setiap WAV dibatasi ke `LimiterCeiling`; mastering voice menyisakan
headroom tambahan agar beberapa event yang berdekatan tidak clipping.

Nama file bundle adalah kontrak runtime. Tambahkan varian baru dengan suffix
dua digit dan daftarkan pada `AudioEngine::Initialize`.

## Refuel

`RefuelInteraction` melacak kendaraan terakhir yang dikemudikan. Syarat isi:

1. pemain sudah turun dan berada maksimal 5 meter dari kendaraan;
2. mesin mati;
3. ada prop pom bensin yang dikenali dalam radius, atau pemain membawa
   `WEAPON_PETROLCAN`;
4. tombol `Refuel` ditahan.

Pom dideteksi dari model native, tanpa daftar koordinat SPBU. Jalur pom
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
- gear HUD default berada di kanan atas agar tidak menabrak minimap atau
  speedometer Menyoo yang umumnya berada di bawah;
- X/Y dan scale dapat diubah live dari `HUD Settings`;
- setiap widget utama dapat dimatikan jika speedometer lain sudah menampilkan
  data yang sama;
- prompt fuel/service berada di tengah bawah dan hanya tampil saat konteksnya
  valid.

## Bundle

Isi `bundle/` disalin ke root GTA V:

```text
manual-trans-w-clutch.asi
manual-trans/
  audio/
    *.wav
```

Jangan menaruh WAV di samping project lama `melar-transmission`; runtime hanya
membaca struktur bundle yang seragam.
