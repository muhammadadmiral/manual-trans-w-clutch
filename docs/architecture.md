# Arsitektur drivetrain

`MainScript` cuma mengatur urutan update. Setiap domain punya state dan
tanggung jawab sendiri.

```text
GTA controls
    │
    ├── PedalModel ─────> brake-throttle override / heel-toe window
    ├── GearLogic ──────> manual Gear / NextGear
    ├── AutomaticGearbox> selector P-R-N-D-S dan shift map
    ├── ClutchSystem ───> fClutch
    ├── EngineModel ────> free-rev, inertia, load, stall
    ├── TractionControl > throttle cut bila CWheel valid
    └── BrakeSystem ────> brake modulation bila CWheel valid
```

## Urutan frame

1. Baca throttle, brake, steer dari control value GTA dan clutch dari binding
   mod.
2. Tentukan mode efektif. EV dikunci ke automatic; mode Off tidak menulis gear.
3. Bentuk travel clutch manual atau coupling torque-converter automatic.
4. Proses selector/shift, pedal overlap, TCS, dan ABS.
5. Tulis gear dan kapasitas clutch. Menekan clutch manual tidak mengubah gear
   menjadi netral.
6. Jalankan free-rev, engine load, stall, launch control, turbo, dan shift
   shock.
7. Update fuel, lampu, HUD, log, dan telemetry.

`VehicleData` menjadi facade tunggal ke memory. Modul domain tidak menghitung
alamat pointer sendiri.

## Aturan fail-open

- Resolver field inti gagal: transmisi tidak diaktifkan.
- Handling pointer gagal: rumus yang butuh inertia/rasio handling tidak
  memaksakan fallback write.
- Telemetry roda gagal: TCS dan ABS tidak memotong input.
- Write opsional selalu no-op bila offset nol.
