# Audio Sprint Plan

Implementasi aktif memakai XAudio2 dari Windows SDK, bukan FMOD. Keputusan ini
menjaga bundle kecil, tidak membutuhkan DLL tambahan, dan cukup untuk sample
PCM pendek yang dipakai mod.

## Lapisan suara

GTA tetap menjadi sumber suara mesin, exhaust, tyre slip, turbo spool, blow-off,
road noise, angin, occlusion kabin, tunnel reverb, dan efek time-scale. Sistem
eksternal tidak mencoba mengganti sound engine per kendaraan.

XAudio2 menambahkan feedback mekanikal:

- bank shift mobil normal/soft/power;
- bank shift motor up/down normal/soft;
- bank missed shift motor;
- parking brake apply/release;
- selector automatic.

Bank dengan beberapa file memakai random non-repeat. Pitch dan volume diberi
variasi kecil per event. Cooldown per bank mencegah spam; pool 16 voice
membatasi concurrency. Peak limiter bekerja saat load dan master voice
menyediakan headroom pasca-mix.

## Kontrak asset

Semua WAV harus PCM, stereo, 44.1 kHz, 16-bit dan berada di:

```text
melar-transmission/audio/
```

Nama file menggunakan `snake_case` dan suffix dua digit, misalnya
`bike_shift_up_01.wav`. Daftar lengkap ada di bundle.

## Integrasi event

- Manual successful shift memilih bank berdasarkan vehicle type, arah,
  throttle, dan clutch.
- Automatic internal shift memakai bank yang sama dengan profil soft pada D
  atau lebih berat pada S.
- Selector P-R-N-D-S-L memakai selector event jika sample tersedia.
- Grind tanpa sample custom tetap fallback ke native GTA sound.
- Parking brake punya bank apply dan release terpisah.
- Native `TURBO_BLOW_OFF` tetap dipicu oleh perubahan boost di `TurboSystem`.

## Lanjutan

Fitur berikut ditunda sampai ada sample yang memang dibuat untuknya:

- custom turbo flutter/stututu, anti-lag, dan exhaust pop;
- true post-mix DSP limiter/XAPO;
- custom 3D panning/attenuation untuk sumber WAV;
- sinkronisasi slider SFX GTA;
- cabin low-pass khusus sample eksternal.

Menunda fitur tersebut mencegah sample generik dipaksakan ke semua kendaraan.
