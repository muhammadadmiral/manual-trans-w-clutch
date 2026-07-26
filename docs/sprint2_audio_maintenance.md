# Sprint 2: Audio Engine & Vehicle Maintenance

Dokumen ini merinci rencana implementasi untuk fase pengembangan selanjutnya (Sprint 2). Mod ini akan dipertahankan sebagai **Satu Kesatuan Supermod** (tidak dipisah menjadi beberapa `.asi`) untuk memastikan sinkronisasi data yang sempurna (tanpa *delay* memori) antara mesin fisika, sistem audio, dan sistem degradasi kendaraan.

---

## 1. Fase Pertama: FMOD Audio Engine Integration
Fokus utama adalah memberikan *feedback* pendengaran (audio) yang sangat akurat terhadap *state* fisika kendaraan yang sudah dibangun di Sprint 1.

### 1.1 Inisialisasi & Setup
- Integrasi FMOD Core API ke dalam *Visual Studio Project*.
- Pembuatan kelas `AudioEngine` untuk me- *load* sample `.wav` / `.ogg` mentah.
- Sistem *dynamic pitch* dan *volume* yang diikat (di- *link*) langsung ke `VehicleData::GetRPM()` dan `VehicleData::GetClutch()`.

### 1.2 Mechanical Transmission Sounds
- **Gear Shift Clunk**: Suara besi tuas transmisi berpindah. Diacak (randomize pitch/sample) agar tidak repetitif.
- **Gear Grind (Clash)**: Suara gigi rontok saat *shift resistance* tinggi (pindah gigi tanpa kopling diinjak penuh). Volume bergantung pada `clashSeverity`.
- **Gear Whine**: Suara dengung transmisi lurus (Straight-cut gears) yang volumenya meningkat seiring kecepatan roda (`WheelAngularVelocity`), khusus untuk mobil dengan profil *Racing/Sport*.

### 1.3 Engine & Forced Induction Sounds
- **Turbo Spool & Flutter (Stututu)**: Menggunakan data dari `TurboSystem::GetBoostPressure()`. Suara *flutter* (Blow-off) diputar saat *throttle* dilepas mendadak dari boost tinggi.
- **Rev-Limiter & Backfire**: Suara ledakan knalpot saat mobil menyentuh *redline* (terutama saat *Money Shift* atau lepas kopling kasar).
- **Shift Shock (DSG Fart)**: Suara letupan knalpot khas DCT/Matic modern saat `AutomaticGearbox` melakukan pemotongan torsi (Ignition Cut 100ms) saat *upshift*.

---

## 2. Fase Kedua: Vehicle Maintenance & Survival
Mengubah mod ini menjadi sistem *survival* kendaraan yang realistis menggunakan data telemetri yang sudah diekstrak oleh `GameMemoryEngine`.

### 2.1 Refueling System (Sistem Bensin)
- **Deteksi Pom Bensin (Tanpa Mapping Manual)**: Menggunakan Native `MISC::GET_CLOSEST_OBJECT_OF_TYPE` untuk mendeteksi *prop* tiang pom bensin (`prop_gas_pump_1d`, dll) di radius tertentu. Berfungsi di seluruh penjuru map Los Santos tanpa perlu mencatat koordinat X,Y,Z.
- **Animasi Pengisian**: Pemain harus turun dari mobil (`!PED::IS_PED_IN_ANY_VEHICLE`), berdiri di dekat mobil, dan menekan tombol interaksi. Karakter akan melakukan animasi pengisian (memutar animasi *medic/jerrycan* bawaan GTA V).
- **Jerry Can (Darurat)**: Mengizinkan pengisian bensin di pinggir jalan jika pemain memiliki *weapon prop* Jerry Can (`WEAPON_PETROLCAN`).

### 2.2 Drivetrain Degradation (Keausan Mesin & Transmisi)
- **Clutch Wear (Plat Kopling Aus)**: Gantung kopling terlalu lama di tanjakan (Clutch Heat tinggi) akan mengurangi *MaxClutchTorque* secara permanen. Jika terlalu aus, kopling akan selip (slip) di gigi tinggi walau pedal dilepas.
- **Synchro Damage**: Sering melakukan salah gigi (Gear Grind) akan merusak sinkronisator. Efek: *Shift delay* bertambah dan perpindahan gigi menjadi nyangkut.
- **ATF Overheat (Limp Mode)**: Transmisi otomatis yang sering dipaksa *kickdown* atau *neutral drop* akan mengalami *overheat* oli transmisi. Jika kepanasan, komputer mobil akan mengunci transmisi maksimal di gigi 3 (Limp Mode).

### 2.3 Tire & Oil Telemetry
- **Tire Wear**: Menggunakan `WheelTelemetry` untuk mendeteksi seberapa sering roda mengalami *wheelspin* (putaran roda jauh lebih cepat dari kecepatan mobil). Ban botak = grip menurun (memanipulasi `HandlingData`).
- **Oil Life**: Kualitas oli menurun seiring jarak tempuh (*odometer*). Oli kotor menyebabkan RPM *idle* tidak stabil (pincang) dan tenaga maksimal mesin (DriveForce) turun 5-10%.

---

## 3. Tahapan Implementasi (Next Steps)
1. Siapkan *sample* suara `.wav` mentah (Turbo, BOV, Gear Shift).
2. Tautkan FMOD `.lib` dan `.dll` ke project.
3. Buat implementasi `AudioEngine.cpp`.
4. *Test Drive*: Tes sinkronisasi suara *Gear Grind* dan *Blow-off Valve* dengan fisika di dalam game.
5. Mulai rancang UI/Teks untuk *Fuel* & *Maintenance*.

## 4. Entry Gate dari Sprint Drivetrain

Sprint 2 baru dibuat di branch terpisah setelah release candidate drivetrain
lulus smoke test berikut:

1. Manual idle take-off bekerja lewat release clutch pelan tanpa gas, tetapi
   dump cepat dan tanjakan tetap dapat stall/rollback.
2. Automatic D/R melakukan creep mengikuti release brake dan tidak membaca
   injeksi creep sebagai input pemain.
3. Faggio tetap sequential, Pizza Boy tetap CVT, dan utility single-speed
   tidak lagi tampil sebagai sequential satu gigi.
4. Motor dan mobil sport tidak kehilangan tenaga gigi 1 sebelum RPM mesin
   aktual mencapai limiter.
5. Netral, reverse, low-RPM gear tinggi, serta manual/automatic regression test
   tetap lulus tanpa crash.

Asset audio, FMOD, dan persistence maintenance tidak masuk ke commit release
candidate drivetrain agar PR ke master tetap mudah direview.
