<div align="center">
  <h1>🏎️ Melar Transmission — GTA V</h1>
  <p><strong>The Ultimate Vehicle Physics & Transmission Mod for Grand Theft Auto V</strong></p>

  [![Build Status](https://img.shields.io/badge/Build-v1.1-brightgreen.svg)]()
  [![Platform](https://img.shields.io/badge/Platform-GTA_V_Enhanced-blue.svg)]()
  [![ScriptHookV](https://img.shields.io/badge/Required-ScriptHookV-orange.svg)]()
</div>

---

## 🌟 Overview
**Melar Transmission** adalah plugin *ScriptHookV open-source* yang merevolusi cara lu mengendarai kendaraan di GTA V. Ucapkan selamat tinggal pada transmisi arcade bawaan game! Mod ini mengubah total fisika kendaraan dengan simulasi *drivetrain* serealistis mungkin: mulai dari *clutch bite point*, *engine stall*, sistem *wear and tear*, hingga dukungan audio mekanis XAudio2.

> [!IMPORTANT]
> **PERSYARATAN SISTEM MINIMAL**
> - **Grand Theft Auto V:** Enhanced Edition (Minimal Build **1.0.1013.20** atau lebih baru)
> - **ScriptHookV:** Versi terbaru yang kompatibel
> - **OS:** Windows 10/11 (64-bit)

---

## 🔥 Fitur Utama (v1.1)

### 🕹️ Mode Transmisi Super Dinamis
- **Manual Sequential:** Oper gigi layaknya mobil balap sungguhan. Lengkap dengan mekanisme kopling, *engine stall*, dan *money shift* (over-rev).
- **Automatic (P-R-N-D-S-L2-L1):** Sistem *Automatic* modern dengan simulasi *Torque Converter Lockup*, *kickdown delay*, *safety-neutral*, dan *DSG Ignition Cut*.
- **Native Vanilla Off:** Matikan mod kapan saja dan kendaraan kembali ke mode arcade GTA bawaan.

### ⚙️ Fisika & Mekanik Realistis
- **Clutch Simulation:** Simulasi pedal kopling dengan kurva *bite point*. Plat kopling bisa *fade/slip* karena panas (heat fade), serta kapasitas torsi yang dinamis!
- **True Engine Stall:** Mesin mati kalau lu salah lepas kopling, RPM terlalu rendah, terguling, atau menerjang genangan air terlalu dalam.
- **Creep & Rev-Hang:** Mobil otomatis merayap (creep) saat di gigi D. Lepas gas di posisi Netral? Ada inertia *rev-hang* khas mesin sungguhan.
- **TCS, ABS, ESC & Launch Control:** Menggunakan data telemetri aktual roda
  (`CWheel`), predictive slip-rate, ABS pulse, selective-yaw ESC, rollover
  mitigation, dan launch control progressive soft-cut.
- **Weight & Drivetrain Flex:** Estimasi massa berbasis kelas/dimensi,
  driveline wind-up, mount compliance, dan suspension load-transfer pitch.

### 🎮 LSC Integration & Wear System
- **Upgrade Dinamis:** Beli upgrade Transmisi/Engine di Los Santos Customs (LSC) akan memengaruhi *stall resistance*, ketahanan *synchro*, penalti perpindahan gigi, hingga kapabilitas *powershift*.
- **Melar Workshop:** Enam pedal map per model, clutch/flywheel/transmission
  package, creep/cruise, drivetrain mount, dan kalibrasi assist. Paket Race
  membuka quick/powershift mobil maupun motor dengan wear nyaris nol.
- **Maintenance Bay:** Sistem umur oli (*oil-life*), bengkel servis untuk *rebuild* gearbox/kopling, dan pengisian bensin (*refuel*) interaktif.

### 🎵 Custom XAudio2 Mechanical Sounds
Audio native GTA V itu membosankan. Melar Transmission punya audio engine sendiri:
- Memuat file `.wav` eksternal secara acak untuk transisi *shift* (Slow, Normal, Harsh).
- Suara *Gear Grind*, blow-off/flutter, *Clutch Slip*, *Gearbox Clunk*,
  engine lug, ABS/TCS/launch cut, drivetrain flex, dan selector.
- *Limiter* dan *Headroom* independen untuk pengalaman audio yang imersif dan tidak pecah.

### 🖥️ Native HUD & Telemetry
Speedometer/tachometer assetless memiliki sembilan layout plus Auto Dynamic
yang berbeda untuk mobil dan motor, icon state on/off, pilihan unit/accent,
log assist, serta overlay kondisi drivetrain.

---

## 📁 Struktur Kode (Modular Architecture)
Sistem sudah dirapikan menggunakan arsitektur modular yang super bersih (*Orchestrator-Controller*) untuk mempermudah pengembangan lanjutan:

```text
src/
├─ Audio/                🎵 XAudio2 loader, random bank, shift sounds limiter
├─ Core/                 ⚙️ Config parser, Menu, Logger, Renderer
├─ Memory/               🧠 AOB Scanners, Resolvers, Memory Wrappers, Kalibrasi rasio
├─ Script/               🎮 Orchestrator utama & Event Bus Controllers
├─ UI/                   🖥️ Speedometer & HUD Renderer khusus
└─ Vehicle/              🏎️ Simulasi Core Fisika:
   ├─ Engine/            Inertia, Stall, Launch Control, Fuel System, Turbo
   ├─ Gearbox/
   │  ├─ Automatic/      PRNDS, adaptive map, TCC/ATF
   │  ├─ Manual/         Manual/sequential shift path
   │  └─ Core/           Handling profile, ratio, health, synchro
   ├─ Clutch/            Heat/Slip simulation, Pedal curves
   ├─ Brakes/            ABS & Parking Brake berbasis CWheel
   ├─ Maintenance/       Workshop tuning, Oil-life, Refuel System
   └─ Physics/           Mass estimate, drivetrain flex, suspension pitch
```
*Dokumentasi detail arsitektur tersedia di [docs/architecture.md](docs/architecture.md).*

---

## 🛠️ Cara Instalasi & Build

### Instalasi untuk Pemain (User):
1. Pastikan **ScriptHookV** sudah terinstall.
2. Salin seluruh isi folder `bundle/` (termasuk `melar-transmission.asi` dan folder `melar-transmission/`) ke direktori root instalasi GTA V lu.

### Untuk Developer (Build from Source):
1. Buka Visual Studio (Workload: *Desktop development with C++*).
2. Buka solusi `melar-transmission.slnx`.
3. Set konfigurasi ke `Release | x64`.
4. Build Project (Ctrl+Shift+B).
5. File ASI akan di-generate di folder `x64/Release/melar-transmission.asi`.

> [!TIP]
> Sesudah mengganti ASI, buka `melar-transmission.log`. Pastikan startup
> mencetak versi `1.1.0` dan path ASI yang benar.

---

## ⚙️ Konfigurasi Penting (`melar-transmission.ini`)
Mod ini bisa lu *tweak* segila mungkin via INI file. Beberapa pengaturan krusial:

```ini
[Transmission]
Mode=2                     ; 0=Mati, 1=Auto PRND, 2=Manual Sequential

[Engine]
StallEnabled=1             ; Mesin bisa mati
LugStallRPM=1500           ; Titik RPM mulai ngos-ngosan
RevHangDuration=0.50       ; Durasi RPM turun (inertia)

[Automatic]
KickdownDelay=0.65         ; Delay respons gas pol di gigi Auto
SportTorqueBoost=0.10      ; Tenaga ekstra di mode S (Sport)

[Maintenance]
FuelEnabled=1              ; Bensin bisa habis
```
*Panduan semua opsi tersedia lengkap di [docs/configuration.md](docs/configuration.md).*

---

## 🛡️ Keselamatan Memori (Anti-Crash)
Melar Transmission nggak nulis ke memori secara brutal.
- *Write* hanya terjadi pada 5 signature offset yang tervalidasi penuh.
- Jika script gagal mendeteksi data add-on mobil, mod akan **beradaptasi otomatis** menghitung kurva gigi ideal berdasarkan *top speed* aslinya.
- TCS/ABS otomatis me-*disable* diri jika struktur roda (`CWheel`) dari memori tidak berhasil di-*resolve*.
- **Fail-Safe:** Matikan mod (Mode=0) kapan saja. Operasi *byte-patch* (`JNE` ke `JMP`) otomatis dikembalikan ke aslinya. *No crash, no hassle.*

---
<div align="center">
  <i>"Karena menyetir di Los Santos bukan sekadar menekan tombol W."</i><br>
  <b>— Melar Transmission</b>
</div>
