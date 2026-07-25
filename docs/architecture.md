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
    ├── EngineModel ────> RPM open-driveline, load, creep, stall
    ├── TractionControl > throttle cut bila CWheel valid
    └── BrakeSystem ────> brake modulation bila CWheel valid
```

## Urutan frame

1. Baca throttle, brake, steer dari control value GTA dan clutch dari binding
   mod.
2. `VehicleProfile` menentukan mobil, EV, scooter CVT, atau motor sequential.
   EV/scooter dikunci ke automatic; mode Off tidak menulis gear.
   `VehicleUpgrades` membaca engine, transmission, dan turbo mod native.
3. Bentuk travel clutch manual atau coupling torque-converter automatic.
4. Proses selector/shift, pedal overlap, TCS, dan ABS.
5. Tulis gear dan signed clutch actuator. Pedal clutch mentok memakai gear 1
   sebagai carrier, tetapi logical gear tidak berubah.
6. Jalankan free-rev hanya bila driveline terbuka, lalu load/stall, launch
   soft-cut, turbo telemetry, dan shift shock.
7. Ulangi write drivetrain sebagai write terakhir, lalu update lampu, HUD, log,
   dan telemetry.

`VehicleData` menjadi facade tunggal ke memory. Modul domain tidak menghitung
alamat pointer sendiri.

## Otoritas engine

Saat driveline terhubung, target RPM berasal dari road speed dan rasio runtime.
Saat netral atau clutch-open, `EngineModel` melanjutkan RPM mesin bebas memakai
inertia. Recovery low-RPM hanya memakai power multiplier native; tidak menulis
runtime drive-force, roda, atau entity speed.

Money-shift wheel-lock memakai pulse engine-brake/pressure pendek karena field
`CWheel` build Enhanced ini belum tervalidasi untuk write per roda. Efeknya
belum driven-wheel-exact. Hill rollback tidak menulis velocity: idle-creep
dibatalkan ketika grade load melebihi kapasitas bite, lalu rigid-body GTA yang
menangani gravitasi.

## Aturan fail-open

- Resolver field inti gagal: transmisi tidak diaktifkan.
- Runtime `CTransmission` tidak masuk rentang masuk akal: flat velocity jatuh
  ke handling, lalu ke estimated top speed dan rasio top gear. Inertia tetap
  memakai handling atau karakter acceleration native.
- Offset `TransmissionDriveForce` selalu dinolkan karena relasi relatif dari
  gear cluster tidak cukup membuktikan bahwa field aman ditulis.
- Telemetry roda gagal: TCS dan ABS tidak memotong input.
- Write opsional selalu no-op bila offset nol.
