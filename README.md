# Manual Transmission with Clutch — GTA V

Plugin ScriptHookV open-source untuk mengganti perilaku transmisi GTA V dengan
mode Off, automatic P-R-N-D-S-L2-L1, atau manual sequential. Simulasi mencakup clutch,
free-rev netral, creep, stall berbasis beban drivetrain, pedal interlock, dan
assist yang hanya aktif kalau telemetry memory-nya tervalidasi.

## Status

Sprint drivetrain aktif. Fokus saat ini:

- netral dan clutch memutus drivetrain tanpa mematikan throttle mesin;
- RPM, limiter, audio mesin, dan top speed tetap dihitung engine native GTA;
- pelepasan clutch gradual memakai actuator clutch internal GTA;
- clutch mentok melakukan hard-disconnect tanpa menghapus logical gear pilihan;
- dump clutch, bog, dan stall memakai RPM, rasio/handling yang tervalidasi,
  lalu fallback data native per kendaraan bila optional pointer tidak tersedia;
- shift manual tanpa clutch tetap masuk, tetapi menghasilkan clash, torque cut,
  shock, wear, dan risiko over-rev saat salah downshift;
- automatic D melakukan shift santai, sedangkan S menahan RPM, lebih cepat
  kickdown, dan memakai sport pedal map tanpa cheat-power global;
- L2/L1 membatasi gear tertinggi; LShift maju di gate selector dan LCtrl kembali;
- Faggio/Faggio2/Faggio3/Pizza Boy diprofilkan sebagai scooter CVT gas-rem,
  motor lain sequential dengan auto-clutch, dan EV dikunci ke automatic;
- selector automatic punya brake interlock serta lockout P/R saat kendaraan
  masih bergerak ke arah yang salah;
- TCS dan ABS memakai telemetry `CWheel`, bukan estimasi RPM palsu;
- launch control opsional memakai soft throttle cut tanpa menulis RPM;
- temperatur rem, brake fade, clutch heat, dan brake-throttle override dapat
  dituning lewat GUI.

Audio kustom belum menjadi bagian sprint ini.

## Struktur

```text
src/
├─ Core/                 input, config, menu, renderer, logger
├─ Memory/               AOB scanner, resolver, wrapper memory, kalibrasi
├─ Script/               orkestrasi per-frame
└─ Vehicle/
   ├─ Engine/            inertia, pedal, load/stall, turbo, fuel, TCS, launch
   ├─ Gears/             manual sequential, automatic PRNDS, gearbox health
   ├─ Clutch/            kurva pedal, slip/heat, actuator drivetrain
   ├─ Brakes/            ABS berbasis roda dan parking brake
   ├─ VehicleData.*      facade memory per kendaraan
   ├─ VehicleProfile.*   EV, scooter CVT, dan motor sequential
   ├─ LightsLogic.*
   └─ TelemetryLogger.*
```

Detail desain ada di [docs/architecture.md](docs/architecture.md), perilaku
drivetrain di [docs/drivetrain.md](docs/drivetrain.md), dan daftar memory field
di [docs/memory-offsets.md](docs/memory-offsets.md). Seluruh opsi GUI/INI
dijelaskan di [docs/configuration.md](docs/configuration.md).

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
[Transmission]
Mode=2

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
IdleCreep=1
StallEnabled=1

[Automatic]
DUpRPM=0.68
SUpRPM=0.90
SportTorqueBoost=0.10
```

`Mode=0` melepas kontrol drivetrain ke GTA, `Mode=1` mengaktifkan automatic
P-R-N-D-S-L2-L1, dan `Mode=2` mengaktifkan manual sequential. Kendaraan
listrik dan scooter CVT selalu memakai automatic.

Throttle, brake, steer, RPM, limiter, dan engine audio tetap berasal dari GTA.
`ClutchAttack` dan `ClutchRelease` membentuk travel clutch digital. S di gear
maju/netral diblok dari reverse axis GTA sehingga fungsinya tetap rem saja.

## Batas keselamatan memory

Write hanya dilakukan pada offset yang punya pola atau relasi layout
terverifikasi. TCS/ABS otomatis tidak mengintervensi bila `CWheel` tidak
ter-resolve. INI lama yang menyimpan clutch sebagai `RPM+0x4` dikoreksi menjadi
`RPM+0xC` saat load.
