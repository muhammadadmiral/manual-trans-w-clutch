# Konfigurasi drivetrain

Semua opsi berikut tersedia dari menu dalam game dan disimpan ke
`melar-transmission.ini` saat menu ditutup atau kembali ke submenu sebelumnya.
Pada boot pertama setelah rename, config lama `manual-trans.ini` dibaca lalu
ditulis ulang ke nama baru.

## Mode transmisi

`Transmission Mode`:

- `OFF`: mod tidak menulis gear atau clutch; gearbox native GTA mengambil alih.
- `AUTOMATIC`: selector P-R-N-D-S-L2-L1 dan shift map custom aktif.
- `MANUAL`: sequential R-N-1-2 dan seterusnya.

Kendaraan listrik, scooter CVT, dan utility single-speed selalu memakai
automatic walaupun konfigurasi global memilih manual. LShift maju dari P
menuju L1; LCtrl kembali menuju P. Faggio tetap sequential; Pizza Boy memakai
CVT gas-rem.

## Pedal / Keyboard

Preset pedal dipilih per model kendaraan hanya dari panel Melar di Los Santos
Customs: `FACTORY`, `SPORT`, `COMFORT`, `SIM RACE`, `CRAWL / VALET`, dan
`ECO TOURING`. Pilihan itu disimpan pada section
`[WorkshopTuning.<MODEL_HASH>]`.

- `SPORT` dan `SIM RACE` mempercepat tip-in/release tanpa menambah peak power.
- `CRAWL / VALET` memberi travel progresif untuk parkir dan gerak sangat
  pelan.
- `ECO TOURING` melembutkan tip-in serta memperpanjang coast untuk cruising.
- `Throttle Attack/Release` membentuk waktu naik dan turun pedal virtual W.
- `Brake Attack/Release` membentuk pressure virtual tombol S.
- `Throttle/Brake Curve` mencampur respons linear dan cubic.
- `Clutch Attack/Release/Curve` membentuk travel digital tombol clutch.

Keyboard tetap menghasilkan target mentah 0/1, tetapi nilai yang dikirim ke
GTA bergerak kontinu. Tap pendek dapat menghasilkan input parsial, sedangkan
menahan tombol tetap mencapai 1.0.

## Engine / Stall

- `Idle Creep` dan `Creep Throttle`: torsi idle saat gear 1/reverse.
- `Engine Stall`, `Stall Rate`, `Stall Clutch`: waktu dan bite minimum sebelum
  stall.
- `Idle Torque`: cadangan torsi kendaraan saat throttle nol.
- `Lug Stall RPM`: batas zona lugging dalam RPM fisik estimasi. Default 1500
  RPM; mesin masih boleh menarik di bawah angka ini.
- `Actual Stall Cutoff`: batas RPM mesin aktual yang mengizinkan stall
  drivetrain. Default 950 RPM. Zona lugging masih dapat mengurangi torsi di
  atas angka ini, tetapi tidak boleh mematikan mesin.
- `Lug Stall Delay`: waktu dasar sebelum lugging yang tidak pulih menjadi
  stall. Timer melambat saat defisit kecil dan dipercepat brake, tanjakan,
  clutch lock, serta gear tinggi.
- `Water Stall Delay`: waktu hydrolock ketika mesin pembakaran terendam.
- `Rollover Stall`: waktu oil-starvation saat kendaraan terbalik. Motor memakai
  cutoff lebih cepat; EV dikecualikan dari dua simulasi mesin pembakaran ini.
- `Rev Hang`: lama RPM free-rev tertahan setelah throttle ditutup.
- `Hard Brake Stall`: stall saat pengereman darurat menjelang berhenti tanpa
  memutus clutch. Jika telemetry ABS roda belum valid, fallback memakai brake,
  deselerasi longitudinal, RPM, dan kecepatan. Throttle aktif dan window
  power-brake/burnout selalu dikecualikan.
- `Fuel Cut Engine Brake`: konsumsi nol pada overrun dan tambahan drag ringan.
- `Starter Interlock`: manual wajib netral/clutch; automatic wajib P/N.
- `Auto Start Needs Brake`: menambah syarat brake pada starter automatic.
- `Launch Control` dan `Launch RPM`: limiter launch manual atau automatic.

`ConnectedRPMSync` tetap dibaca dari INI lama untuk kompatibilitas. Sinkronisasi
baru memakai inertia, rasio aktual, road speed, dan coupling sehingga nilainya
tidak lagi menjadi faktor tunggal.

## Clutch

- `Pedal Attack/Release/Expo`: respons binding digital.
- `Bite Start/End`: rentang travel yang mengubah torque capacity.
- `Heat/Cool Rate`, `Fade Start/Strength`: temperatur dan slip clutch.
- `Max Clutch Torque`: kapasitas normalized disc; torsi di atas kapasitas
  membuat disc selip walau pedal dilepas.
- `Hot Clutch Judder`: micro-oscillation coupling pada bite point panas.
- `Dump Rate`: kecepatan release minimum yang dianggap clutch dump.
- `Dump Shock`: intensitas feedback dump.

## Automatic D / S / L

- `Brake Interlock`: syarat brake dan direction lock selector.
- `Shift Delay`: dwell minimum untuk mencegah hunting.
- `D/S Upshift RPM` dan `D/S Downshift RPM`: dasar shift map; throttle
  menggeser threshold secara dinamis.
- Baseline D adalah `0.50/0.22`, supaya throttle ringan cepat memilih gear
  tinggi dan bertahan di RPM rendah. Baseline S `0.84/0.34`.
- `Kickdown Pedal`: bukaan throttle minimum untuk downshift paksa yang masih
  aman terhadap over-rev.
- `Kickdown Delay`: reaksi hydraulic/TCU sebelum turun sampai dua gear.
- `Torque Converter Lock`: TCC solid di gear 3+ saat cruise.
- `Fluid Overheat / Limp`: ATF panas membatasi transmisi ke gear 3.
- `Neutral Drop Damage`: N ke D/S pada RPM tinggi memberi shock dan damage.
- `Brake Boost Stall`: gas+rem penuh terlalu lama memanaskan converter dan
  akhirnya mematikan mesin.
- `Throttle/Brake Attack/Release`: actuator khusus automatic; penting untuk
  mengubah input keyboard 0/1 menjadi permintaan torsi dan pressure bertahap.
- `S Torque Boost`: nama legacy untuk agresivitas sport pedal map; tidak
  menambah peak power atau memakai cheat-power native.
- `D Keyboard Ceiling`: batas maksimum pedal W di selector D. Default `1.00`
  memberi rentang penuh dan attack/release yang mengatur transisinya. Nilai
  lebih rendah dapat dipakai sebagai profil cruise pribadi.
- `Brake Overrides Gas`, `Override Delay`, `Override Cut`: ECU pedal overlap.

## Gearbox Penalty

- `Gear Clash`: aktifkan mismatch penalty.
- `Grind Damage`: wear shift tanpa clutch.
- `Shift Shock`: torque cut/getaran ketika RPM input-output tidak sinkron.
- `No-lift Penalty`: tambahan clash bila shift sambil gas tetap dibuka.
- `Synchronizer Wear`: grind menambah wear gear tujuan dan tidak pulih sendiri
  selama sesi drivetrain.
- `Shift Resistance`: synchronizer aus menambah delay; mismatch berat dapat
  menolak shift.
- `Native Gearbox Override`: menahan auto-shift/throttle suppression native
  lewat patch branch satu byte. Matikan untuk diagnosis fail-open; byte asli
  direstore tanpa restart.
- `Reverse Lockout km/h`: kecepatan maksimum untuk memasukkan reverse.
- `Over-rev Damage`: kerusakan money shift.

## ABS / TCS dan brake fade

TCS dan ABS hanya mengintervensi bila minimal dua sample roda valid. Angka
`TCS=0` atau `ABS=0` di log berarti sistem sedang tidak memotong input, bukan
fiturnya mati. Cek `TCSEn`/`ABSEn` untuk konfigurasi dan
`TCSReady`/`ABSReady` untuk kesiapan telemetry roda.

- `Slip Target`: slip yang masih diizinkan.
- `Max Cut/Release`: batas intervensi throttle atau pressure.
- `Brake Heat/Cool Rate`: laju temperatur.
- `Brake Fade Start/Strength`: ambang dan kehilangan tekanan maksimum.

## Smoke test setelah memasang build

1. Manual N: W harus menaikkan RPM tanpa gerak.
2. Manual gear 1 tanpa gas: release clutch pelan harus mulai bergerak lewat
   bite point; dump cepat dapat stall. Ulangi di tanjakan untuk memastikan
   idle drive menyerah dan rollback tetap mungkin.
3. Manual N ke gear 2 dan low-RPM upshift: gear tetap masuk dan throttle tetap
   aktif, tetapi acceleration berat sesuai torque reserve. Di bawah 1500 RPM,
   `Lug` boleh naik; `Stall` harus turun lagi kalau kendaraan berhasil
   berakselerasi atau clutch diinjak.
4. Manual R: W mundur, S mengerem dan tidak menaikkan RPM.
5. Automatic: tahan brake, tekan LShift dari P sampai D, lepas brake bertahap
   untuk creep. L2 hanya memakai gear 1-2 dan L1 mengunci gear 1.
6. Bandingkan D dan S: S harus menahan gear lebih lama, kickdown lebih dini,
   dan memberi respons torsi lebih kuat.
7. Coba P/R saat masih melaju: selector harus menolak perpindahan.
8. D dengan throttle parsial harus naik gear lebih awal dan mempertahankan RPM
   rendah. Full throttle memang meminta kickdown.
9. Bandingkan kendaraan stock dan engine/transmission upgrade. Cek
   `EngMod`, `TransMod`, `Race`, `ShiftQuick`, `ShiftPower`, dan
   `ShiftSynchro` di log.
10. Manual gear 1: tahan gas+rem. Throttle tidak boleh dipotong atau memicu
    stall palsu; saat roda penggerak mulai berputar, `Burnout=1` dan tachometer
    mengikuti wheel telemetry walaupun body speed hampir nol.
11. Cek limiter mobil enam gigi 240-250 km/h. Handling normal umumnya berada
    sekitar 43-45 km/h (gigi 1) dan 84-89 km/h (gigi 2). Add-on dengan handling
    rusak harus menunjukkan `Adaptive=1` di log.

Baris `STATUS` di `melar-transmission.log` dicatat setelah final drivetrain write dan
memuat profile kendaraan, selector, logical/physical gear, clutch, RPM native,
ratio, max velocity, status handling pointer, torque reserve, stall, clash,
money shift, starter, TCS, ABS, dan launch control. `Accel` adalah respons
longitudinal terfilter dan `LowRec` adalah kompensasi torque native yang sedang
dipakai ketika GTA menahan forced gear di low RPM. Untuk assist, suffix `En`
berarti enabled, `Ready` berarti telemetry tersedia, dan nilai tanpa suffix
berarti sedang mengintervensi. Launch control memang mati jika `LCEn=0`;
aktifkan `LaunchControl=1` lewat menu atau bagian `[Engine]`.

`ClutchDemand/ClutchCap/OSlip/Judder`, `SyncWear/ResistMs/ShiftReject`,
`WheelLock`, `HillRollback`, dan `FuelCut` melacak edge case manual. Automatic
menambah `TCC`, `ATF`, `Limp`, `KDPending`, `NeutralDrop`, `BrakeBoost`,
`AutoTM`, dan `IgnCut`.

`Condition` berasal dari engine health. Nilai upgrade dibaca langsung dari mod
kendaraan, bukan INI. `LowRec` dan `PowerMul` adalah jalur recovery low-RPM;
tidak ada write runtime drive-force. `IDLE_DRIVE` memisahkan creep virtual,
hill rollback, dan power multiplier supaya input pemain tidak tertukar dengan
idle governor.

## Sprint 2: audio, fuel, maintenance, dan HUD

```ini
[Audio]
Enabled=1
MasterVolume=0.72
PitchRandomness=0.045
LimiterCeiling=0.72
NativeLayers=1
TurboSounds=1
ClutchSounds=1
TransmissionSounds=1
EngineLoadSounds=1
AssistSounds=1

[Maintenance]
FuelEnabled=1
FuelBlips=1
RefuelRatePerSecond=0.035
Enabled=1
OilWearMultiplier=1.0

[Overlay]
GearHud=1
GearPosX=0.90
GearPosY=0.20
GearScale=1.0
MenuPosX=0.695
MenuPosY=0.105
MenuScale=1.0

[Speedometer]
Enabled=1
CarStyle=0
BikeStyle=0
Units=0
Accent=0
Detailed=1
PosX=0.815
PosY=0.790
Scale=1.0
Opacity=0.92

[Workshop]
Enabled=1
Radius=14.0
```

`LimiterCeiling` diterapkan saat WAV dimuat, jadi perubahan nilai ini perlu
restart game. Lima toggle kategori dapat dimatikan terpisah. `Enabled=0`
mematikan WAV dan fallback sound native milik mod; audio kendaraan GTA yang
tidak dimiliki mod tetap berjalan.

`Refuel` dan `OilService` berada di bagian `[Controls]` dan dapat diubah dari
menu. Nilai default masing-masing adalah virtual-key `69` (E) dan `79` (O).
`FuelBlips=1` menampilkan pom sebagai blip short-range di minimap.
Gear HUD sengaja default di kanan atas agar tidak menabrak minimap maupun
speedometer Menyoo. Posisi dan scale dapat disetel live.

## Speedometer Studio

Speedometer dirender langsung dengan primitive native GTA, jadi tidak
membutuhkan DLL UI, SVG, texture dictionary, atau file aset tambahan. Data
speed memakai `GET_ENTITY_SPEED`, sedangkan RPM/gear berasal dari state yang
sama dengan drivetrain frame aktif.

- Mobil: `GT DIGITAL`, `TWIN ANALOG`, `SPORT HYBRID`, `RETRO TOURING`,
  `MINIMAL DIGITAL`, `ARC DIGITAL`, `RALLY STACK`, `LUXURY CLUSTER`,
  `TRACK BAR`, dan `AUTO DYNAMIC`.
- Motor/quad: `ROAD TFT`, `ROUND CLASSIC`, `SUPERSPORT TFT`, `CAFE RACER`,
  `MINIMAL ENDURO`, `ARC TFT`, `MOTOCROSS STACK`, `CRUISER TWIN`,
  `TRACK BAR`, dan `AUTO DYNAMIC`.
- `Units`: km/h atau mph.
- `Accent`, `Position`, `Scale`, dan `Opacity`: customization live.
- `Detailed Telemetry`: fuel, temperatur/life oli, engine/gearbox health,
  clutch heat, boost, odometer, TCS, ABS, launch control, parking brake,
  burnout, dan service warning.

## Los Santos Customs integration

`LS Customs Service Bays` menambahkan menu Melar ketika kendaraan berhenti di
dekat Los Santos Customs Burton/La Mesa/LSIA/Harmony, Beeker's Garage, atau
Benny's. Tekan key `Workshop` (default E). Service mekanikal mewajibkan mesin
mati; tuning boleh dilakukan saat mesin hidup.

Panel menyediakan pedal, clutch package, flywheel, transmission, creep,
cruise/coast, drivetrain mounts, TCS, ABS, launch, dan empat tindakan servis.
Paket transmisi `RACE Q/P-SHIFT` membuka quick/powershift untuk mobil dan
motor dengan wear nyaris nol. Street/Sport tetap meningkatkan shift stage
tanpa mengganti max RPM, final ratio, atau top-speed handling native.
`Workshop Radius` dapat diperbesar untuk map overhaul.

Contoh section yang dibuat otomatis:

```ini
[WorkshopTuning.1A2B3C4D]
PedalMap=4
ClutchPackage=1
Flywheel=2
Transmission=3
CreepCalibration=3
CruiseCalibration=2
DrivetrainMounts=1
TCSCalibration=2
ABSCalibration=0
LaunchCalibration=2
```

Nilai disimpan sebagai index sesuai urutan label pada panel; gunakan panel
untuk mengubahnya agar range selalu tervalidasi.
