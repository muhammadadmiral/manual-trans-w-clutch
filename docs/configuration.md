# Konfigurasi drivetrain

Semua opsi berikut tersedia dari menu dalam game dan disimpan ke
`manual-trans.ini` saat menu ditutup atau kembali ke submenu sebelumnya.

## Mode transmisi

`Transmission Mode`:

- `OFF`: mod tidak menulis gear atau clutch; gearbox native GTA mengambil alih.
- `AUTOMATIC`: selector P-R-N-D-S-L2-L1 dan shift map custom aktif.
- `MANUAL`: sequential R-N-1-2 dan seterusnya.

Kendaraan listrik dan scooter CVT selalu memakai automatic walaupun konfigurasi
global memilih manual. LShift maju dari P menuju L1; LCtrl kembali menuju P.
Motor non-scooter tetap sequential dan memakai auto-clutch.

## Engine / Stall

- `Idle Creep` dan `Creep Throttle`: torsi idle saat gear 1/reverse.
- `Engine Stall`, `Stall Rate`, `Stall Clutch`: waktu dan bite minimum sebelum
  stall.
- `Idle Torque`: cadangan torsi kendaraan saat throttle nol.
- `Lug Stall RPM`: batas zona lugging dalam RPM fisik estimasi. Default 1500
  RPM; mesin masih boleh menarik di bawah angka ini.
- `Lug Stall Delay`: waktu dasar sebelum lugging yang tidak pulih menjadi
  stall. Timer melambat saat defisit kecil dan dipercepat brake, tanjakan,
  clutch lock, serta gear tinggi.
- `Water Stall Delay`: waktu hydrolock ketika mesin pembakaran terendam.
- `Rollover Stall`: waktu oil-starvation saat kendaraan terbalik. Motor memakai
  cutoff lebih cepat; EV dikecualikan dari dua simulasi mesin pembakaran ini.
- `Rev Hang`: lama RPM free-rev tertahan setelah throttle ditutup.
- `Hard Brake Stall`: stall saat pengereman darurat menjelang berhenti tanpa
  memutus clutch. Jika telemetry ABS roda belum valid, fallback memakai brake,
  deselerasi longitudinal, RPM, dan kecepatan.
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
- `D Keyboard Pedal`: batas intent throttle tombol W di selector D. Keyboard
  tidak punya pedal parsial, jadi default `0.62` menjaga D tetap santai;
  selector S tetap menerima W sebagai full throttle.
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
2. Manual gear 1 + clutch + W: RPM naik; release pelan creep/berangkat, release
   cepat memberi dump; throttle kecil dapat stall.
3. Manual N ke gear 2 dan low-RPM upshift: gear tetap masuk dan throttle tetap
   aktif, tetapi acceleration berat sesuai torque reserve. Di bawah 1500 RPM,
   `Lug` boleh naik; `Stall` harus turun lagi kalau kendaraan berhasil
   berakselerasi atau clutch diinjak.
4. Manual R: W mundur, S mengerem dan tidak menaikkan RPM.
5. Automatic: tahan brake, tekan LShift dari P sampai D, lepas brake untuk
   creep. L2 hanya memakai gear 1-2 dan L1 mengunci gear 1.
6. Bandingkan D dan S: S harus menahan gear lebih lama, kickdown lebih dini,
   dan memberi respons torsi lebih kuat.
7. Coba P/R saat masih melaju: selector harus menolak perpindahan.
8. D dengan throttle parsial harus naik gear lebih awal dan mempertahankan RPM
   rendah. Full throttle memang meminta kickdown.
9. Bandingkan kendaraan stock dan engine/transmission upgrade. Cek
   `EngMod`, `TransMod`, `Race`, `ShiftQuick`, `ShiftPower`, dan
   `ShiftSynchro` di log.

Baris `STATUS` di `manual-trans.log` dicatat setelah final drivetrain write dan
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
kendaraan, bukan INI. `LowRec` dan `PowerMul` adalah satu-satunya jalur
recovery low-RPM pada r18-safe-takeover; tidak ada write runtime drive-force
atau executable `.text` GTA.
