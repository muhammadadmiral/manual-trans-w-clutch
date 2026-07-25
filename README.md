# Manual Transmission with Clutch — GTA V

Plugin ScriptHookV open-source untuk mengganti perilaku transmisi GTA V dengan
gear manual sequential, clutch analog/digital, free-rev netral, stall berbasis
beban drivetrain, serta assist yang hanya aktif kalau telemetry memory-nya
tervalidasi.

## Status

Sprint drivetrain aktif. Fokus saat ini:

- netral dan clutch memutus drivetrain tanpa mematikan throttle mesin;
- RPM bebas mengikuti throttle dan `fDriveInertia` kendaraan;
- pelepasan clutch gradual memakai actuator clutch internal GTA;
- dump clutch, bog, dan stall dihitung dari RPM, rasio gear, kecepatan roda,
  serta `fDriveMaxFlatVel`;
- TCS dan ABS memakai telemetry `CWheel`, bukan estimasi RPM palsu;
- launch control opsional dengan target RPM yang bisa diatur.

Audio kustom belum menjadi bagian sprint ini.

## Struktur

```text
src/
├─ Core/                 input, config, menu, renderer, logger
├─ Memory/               AOB scanner, resolver, wrapper memory, kalibrasi
├─ Script/               orkestrasi per-frame
└─ Vehicle/
   ├─ Engine/            inertia, load/stall, turbo, fuel, TCS, launch control
   ├─ Gears/             pemilihan gear dan kesehatan gearbox
   ├─ Clutch/            kurva pedal, slip/heat, actuator drivetrain
   ├─ Brakes/            ABS berbasis roda dan parking brake
   ├─ VehicleData.*      facade memory per kendaraan
   ├─ LightsLogic.*
   └─ TelemetryLogger.*
```

Detail desain ada di [docs/architecture.md](docs/architecture.md), perilaku
drivetrain di [docs/drivetrain.md](docs/drivetrain.md), dan daftar memory field
di [docs/memory-offsets.md](docs/memory-offsets.md).

## Build

1. Siapkan Visual Studio dengan workload Desktop development with C++.
2. Buka `manual-trans-w-clutch.vcxproj`.
3. Pilih `Release | x64`.
4. Build project.
5. Salin `x64/Release/manual-trans-w-clutch.asi` ke folder GTA V yang sudah
   memuat ScriptHookV.

Artefak yang sudah diverifikasi pada sprint ini:

```text
x64/Release/manual-trans-w-clutch.asi
```

## Konfigurasi penting

```ini
[Controls]
ShiftUp=160
ShiftDown=162
ClutchKey=88

[Analog]
ClutchAttack=0.045
ClutchRelease=0.060

[Engine]
LaunchControl=0
LaunchControlRPM=0.72

[Memory]
AllowIniFallback=1
```

Throttle, brake, dan steer dibaca dari control value GTA. `ClutchAttack` dan
`ClutchRelease` hanya membentuk pedal digital; pedal analog nantinya dapat
memberikan travel langsung.

## Batas keselamatan memory

Write hanya dilakukan pada offset yang punya pola atau relasi layout
terverifikasi. TCS/ABS otomatis tidak mengintervensi bila `CWheel` tidak
ter-resolve. INI lama yang menyimpan clutch sebagai `RPM+0x4` dikoreksi menjadi
`RPM+0xC` saat load.
