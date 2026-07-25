# Manual Transmission with Clutch — GTA V

Plugin ScriptHookV open-source untuk mengganti perilaku transmisi GTA V dengan
mode Off, automatic P-R-N-D-S-L2-L1, atau manual sequential. Simulasi mencakup clutch,
free-rev netral, creep, stall berbasis beban drivetrain, pedal interlock, dan
assist yang hanya aktif kalau telemetry memory-nya tervalidasi.

## Status

Sprint drivetrain aktif. Fokus saat ini:

- netral dan clutch memutus drivetrain tanpa mematikan throttle mesin;
- gearbox native Enhanced yang memaksa shift/clutch/throttle diambil alih lewat
  lima signature tervalidasi; pemasangan atomik dan selalu punya rollback;
- RPM netral/clutch-open memakai free-rev inertia, sedangkan RPM tersambung
  mengikuti kecepatan jalan dan rasio gear aktif tanpa menulis velocity;
- pelepasan clutch gradual memakai actuator clutch internal GTA;
- clutch mentok melakukan hard-disconnect tanpa menghapus logical gear pilihan;
- dump clutch, bog, dan stall memakai RPM, rasio/handling yang tervalidasi,
  lalu fallback data native per kendaraan bila optional pointer tidak tersedia;
- shift manual tanpa clutch tetap masuk, tetapi menghasilkan clash, torque cut,
  shock, synchronizer wear/resistance, dan risiko over-rev + skid pulse saat
  salah downshift;
- clutch punya kapasitas torsi, overload slip, heat fade, serta judder di bite
  point; idle creep menyerah pada tanjakan bila torsinya tidak cukup;
- free-rev punya rev-hang berbasis inertia, pengereman darurat tanpa clutch bisa
  stall, dan overrun memakai fuel cut;
- automatic D melakukan shift santai, sedangkan S menahan RPM, lebih cepat
  kickdown, memberi downshift blip, dan memakai sport pedal map;
- automatic memodelkan TCC lockup, kickdown delay, temperatur ATF/limp mode,
  neutral-drop damage, DSG ignition cut, brake-boost stall, dan safety-neutral;
- L2/L1 membatasi gear tertinggi; LShift maju di gate selector dan LCtrl kembali;
- Faggio/Faggio2/Faggio3/Pizza Boy diprofilkan sebagai scooter CVT gas-rem,
  motor lain sequential dengan auto-clutch, dan EV dikunci ke automatic;
- selector automatic punya brake interlock serta lockout P/R saat kendaraan
  masih bergerak ke arah yang salah;
- TCS dan ABS memakai telemetry `CWheel`, bukan estimasi RPM palsu;
- engine/transmission upgrade native memengaruhi stall resistance, durability,
  quickshift, powershift, dan shift penalty;
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

Sesudah mengganti ASI, cek awal `manual-trans.log`. Build sprint ini wajib
mencetak `Runtime=driveline-r18-safe-takeover` dan path file yang benar-benar dimuat. Kalau
baris itu tidak ada, GTA masih memakai salinan ASI lama.

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
LugStallRPM=1500
LugStallDelay=2.20
WaterStallDelay=2.50
RolloverStallDelay=7.00
RevHangDuration=0.50
HardBrakeStall=1
FuelCutoffEngineBrake=1

[Automatic]
DUpRPM=0.50
DDownRPM=0.22
SUpRPM=0.84
SDownRPM=0.34
SportTorqueBoost=0.10
DKeyboardThrottle=0.62
KickdownDelay=0.65
TCC=1
FluidOverheat=1
NeutralDropDamage=1
BrakeBoostStall=1
```

`Mode=0` melepas kontrol drivetrain ke GTA, `Mode=1` mengaktifkan automatic
P-R-N-D-S-L2-L1, dan `Mode=2` mengaktifkan manual sequential. Kendaraan
listrik dan scooter CVT selalu memakai automatic.

Throttle, brake, dan steer tetap berasal dari control GTA. RPM tersambung
berasal dari rasio dan road speed; throttle hanya memengaruhi seberapa cepat
kendaraan mencapai road RPM itu. Roda dan vehicle speed tidak pernah ditulis.
`ClutchAttack` dan `ClutchRelease` membentuk travel clutch digital. S di gear
maju/netral diblok dari reverse axis GTA sehingga fungsinya tetap rem.

## Batas keselamatan memory

Write hanya dilakukan pada offset yang punya pola atau relasi layout
terverifikasi. TCS/ABS otomatis tidak mengintervensi bila `CWheel` tidak
ter-resolve. Cluster engine memakai `Clutch=RPM+0xC` dan
`EngineThrottle=RPM+0x10`; field throttle ini bukan pedal input. Saat mode
transmisi aktif, RPM dan engine-throttle dapat ditulis untuk menjaga
sinkronisasi poros. Executable code patch dikarantina pada GTA V Enhanced
karena kedua strategi patch flow-control terbukti crash di game thread.
Takeover r18 hanya memakai field drivetrain tervalidasi dan native controls;
tidak ada write ke `.text` GTA. Byte legacy patch tetap punya rollback
saat mod Off, keluar kendaraan, atau unload.
