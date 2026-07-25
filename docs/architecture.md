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

Saat driveline terhubung, `fCurrentRPM` tetap milik GTA. Saat netral atau
clutch-open, `EngineModel` sementara menulis RPM dan engine-throttle agar mesin
bisa free-rev; model memakai state RPM GTA, inertia/acceleration kendaraan, dan
frame time, bukan template kecepatan per gear. Roda dan entity speed tidak
ditulis. Native power multiplier hanya memberi karakter torsi S dan kembali
`1.0` saat mode Off atau sesi selesai.

## Aturan fail-open

- Resolver field inti gagal: transmisi tidak diaktifkan.
- Runtime `CTransmission` tidak masuk rentang masuk akal: flat velocity jatuh
  ke handling, lalu ke estimated top speed dan rasio top gear. Inertia tetap
  memakai handling atau karakter acceleration native.
- Telemetry roda gagal: TCS dan ABS tidak memotong input.
- Write opsional selalu no-op bila offset nol.
