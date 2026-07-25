# Arsitektur drivetrain

`MainScript` cuma mengatur urutan update. Setiap domain punya state dan
tanggung jawab sendiri.

```text
GTA controls
    │
    ├── PedalModel ─────> brake-throttle override / heel-toe window
    ├── GearLogic ──────> manual Gear / NextGear
    ├── AutomaticGearbox> selector P-R-N-D-S-L2-L1 dan shift map
    ├── ClutchSystem ───> engagement / slip / heat / signed actuator
    ├── EngineModel ────> observasi RPM, load, creep, stall
    ├── TractionControl > throttle cut bila CWheel valid
    └── BrakeSystem ────> brake modulation bila CWheel valid
```

## Urutan frame

1. Baca throttle, brake, steer dari control value GTA dan clutch dari binding
   mod.
2. `VehicleProfile` menentukan mobil, EV, scooter CVT, atau motor sequential.
   EV/scooter dikunci ke automatic; mode Off tidak menulis gear.
3. Bentuk travel clutch manual atau coupling torque-converter automatic.
4. Proses selector/shift, pedal overlap, TCS, dan ABS.
5. Tulis gear dan signed clutch actuator. Pedal clutch mentok memakai gear 1
   sebagai carrier free-rev, tetapi logical gear tidak berubah.
6. Jalankan load/stall, launch soft-cut, turbo telemetry, dan shift shock.
7. Ulangi write drivetrain sebagai write terakhir, lalu update lampu, HUD, log,
   dan telemetry.

`VehicleData` menjadi facade tunggal ke memory. Modul domain tidak menghitung
alamat pointer sendiri.

## Otoritas engine

Mod tidak menulis `fCurrentRPM` atau throttle internal. Ini mencegah satu
template RPM/kecepatan diterapkan ke semua kendaraan dan menjaga first-person
tachometer memakai state yang sama dengan audio engine. Write memory aktif
dibatasi ke gear, next gear, dan clutch. Native power multiplier hanya memberi
karakter torsi S dan kembali `1.0` saat mode Off atau sesi selesai.

## Aturan fail-open

- Resolver field inti gagal: transmisi tidak diaktifkan.
- Handling pointer gagal: load/stall beralih ke native estimated speed dan
  acceleration; tidak ada fallback write ke RPM.
- Telemetry roda gagal: TCS dan ABS tidak memotong input.
- Write opsional selalu no-op bila offset nol.
