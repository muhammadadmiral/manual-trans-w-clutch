# Arsitektur drivetrain

`MainScript` cuma mengatur urutan update. Setiap domain punya state dan
tanggung jawab sendiri.

```text
GTA controls
    │
    ├── GearLogic ──────> Gear / NextGear
    ├── ClutchSystem ───> fClutch
    ├── EngineModel ────> free-rev, inertia, load, stall
    ├── TractionControl > throttle cut bila CWheel valid
    └── BrakeSystem ────> brake modulation bila CWheel valid
```

## Urutan frame

1. Baca throttle, brake, steer dari control value GTA dan clutch dari binding
   mod.
2. Bentuk travel clutch lalu proses permintaan shift.
3. Tulis gear terpilih. Menekan clutch tidak mengubah gear menjadi netral.
4. Tulis kapasitas clutch internal.
5. Jalankan free-rev, engine load, stall, launch control, turbo, TCS, dan ABS.
6. Update fuel, lampu, HUD, log, dan telemetry.

`VehicleData` menjadi facade tunggal ke memory. Modul domain tidak menghitung
alamat pointer sendiri.

## Aturan fail-open

- Resolver field inti gagal: transmisi tidak diaktifkan.
- Handling pointer gagal: rumus yang butuh inertia/rasio handling tidak
  memaksakan fallback write.
- Telemetry roda gagal: TCS dan ABS tidak memotong input.
- Write opsional selalu no-op bila offset nol.
