# Melar Transmission — Overhaul & Modularisasi v1.1

**Dari:** `r25` (test build, 1 file, 1078 baris)  
**Ke:** `v1.1` (production release, arsitektur modular dan dynamic drivetrain)

---

## User Review Required

> [!IMPORTANT]
> Ini adalah overhaul besar. Total ada **8 Controller baru**, **5 sistem yang di-enhance**, dan **3 sistem baru**. Semua logika lama dipertahankan dan diperkaya. Tidak ada yang dihapus.

> [!WARNING]
> Karena besarnya perubahan, eksekusi akan dilakukan bertahap per-kelompok. Setelah setiap kelompok selesai, lu bisa kompile dan test dulu sebelum lanjut.

---

## Gambaran Arsitektur v1.1

```
src/Script/
├── MainScript.cpp              ← Pure orchestrator, ~200 baris
├── MainScript.h
└── Controllers/                ← FOLDER BARU — 8 Controller
    ├── EngineController.h/.cpp
    ├── SignalController.h/.cpp
    ├── CalibrationController.h/.cpp
    ├── TransmissionController.h/.cpp
    ├── HUDController.h/.cpp
    ├── VehicleSessionController.h/.cpp
    ├── DriveAssistController.h/.cpp   ← BARU: TCS+ABS+LC terintegrasi
    └── DiagnosticsController.h/.cpp  ← BARU: Status log + Telemetry
```

---

## Bagian 1 — Refactoring MainScript (Modularisasi)

*Kode yang dipindah, bukan dihapus.*

### Controller 1: `EngineController`
**File:** `src/Script/Controllers/EngineController.h/.cpp`

Ambil dari `MainScript.cpp` baris 177–479. Ngurusin:
- Cold start logic & starter fatigue (makin sering gagal start → makin lama)
- Engine mismatch correction (kalau GTA nge-force engine on/off)
- EV vs ICE start logic
- **Enhancement baru:** State machine lebih ketat: `OFF → CRANKING → RUNNING → STALLED`

```cpp
class EngineController {
public:
    enum class State { Off, Cranking, Running, Stalled };

    void Initialize(Vehicle veh, VehicleProfile::Drivetrain profile, bool coldStart);
    void Update(Vehicle veh, VehicleProfile::Drivetrain profile, bool actualEngineOn);
    void Reset();

    bool IsOn()      const { return m_state == State::Running; }
    bool IsStarting() const { return m_state == State::Cranking; }
    bool IsStalled()  const { return m_state == State::Stalled; }
    State GetState()  const { return m_state; }
    void ForceStall();  // ← baru: dipanggil dari drivetrain stall event

private:
    State     m_state             = State::Off;
    bool      m_engineStarting    = false;
    ULONGLONG m_engineStartTick   = 0;
    ULONGLONG m_lastAttemptTick   = 0;
    ULONGLONG m_starterRequiredMs = 450;
    float     m_starterFatigue    = 0.0f;
};
```

---

### Controller 2: `SignalController`
**File:** `src/Script/Controllers/SignalController.h/.cpp`

Ambil dari `MainScript.cpp` baris 481–509.  Ngurusin:
- Toggle kiri/kanan/hazard
- Auto-cancel saat belok
- **Enhancement baru:** Flash rate dynamis (hazard lebih cepat saat emergency brake)

```cpp
class SignalController {
public:
    void Update(Vehicle veh, float rawBrake, float speedKmH);
    void Reset();
    int  GetActiveSignal() const;

private:
    int m_activeSignal = 0;
};
```

---

### Controller 3: `CalibrationController`
**File:** `src/Script/Controllers/CalibrationController.h/.cpp`

Ambil fungsi `DrawCalibrationHUD` (baris 98–140) dan logika calibration path (baris 511–557). Ngurusin:
- State machine kalibrasi AOB offset
- HUD progress kalibrasi
- Validasi layout offset di kendaraan nyata

```cpp
class CalibrationController {
public:
    void Update(Vehicle veh, bool isEngineOn, float smoothThrottle, int maxGear, VehicleData& data);
    bool IsLayoutValid()   const;
    bool IsCalibrated()    const;
    void ResetLayout();

private:
    CalibrationState m_lastState    = CalibrationState::None;
    bool m_layoutValid              = false;
    bool m_layoutChecked            = false;
    void DrawHUD(CalibrationState state, float smoothedThrottle);
};
```

---

### Controller 4: `TransmissionController` (Core Physics)
**File:** `src/Script/Controllers/TransmissionController.h/.cpp`

Ambil dari baris 579–873 (INTI terbesar). Ngurusin seluruh physics loop per-frame:
- Mode switch (Manual/Auto/Off) & notifikasi
- Kalkulasi clutch, gear, pedal
- Delegasi ke `GearLogic`, `AutomaticGearbox`, `GearboxSystem`, `PedalModel`
- Memory writes (`ApplyToMemory`)
- Drivetrain stall detection

```cpp
class TransmissionController {
public:
    void Update(Vehicle veh, VehicleData& data, VehicleProfile::Drivetrain profile,
                bool isEngineOn, bool workshopOpen, float vehicleSpeed, int maxGear);
    void Reset();

    int   GetManualGear()       const;
    int   GetMode()             const;
    float GetSimulatedClutch()  const;
    float GetDriveThrottle()    const;
    float GetBrake()            const;
    bool  ConsumedStallEvent(); // consume & clear stall flag untuk EngineController

private:
    int       m_manualGear           = 0;
    int       m_activeMode           = -1;
    ULONGLONG m_automaticClutchUntil = 0;
    bool      m_patchFailureShown    = false;
    bool      m_firstFrameTrace      = true;
    int       m_grindWarningTimer    = 0;
    bool      m_stallEventPending    = false;
};
```

---

### Controller 5: `DriveAssistController` ← **SISTEM BARU**
**File:** `src/Script/Controllers/DriveAssistController.h/.cpp`

Sebelumnya TCS, ABS, dan LaunchControl masing-masing dipanggil terpisah di `MainScript.cpp` dengan variabel berceceran. Controller ini menyatukan dan memperkaya semua **driver assistance system** jadi satu entitas.

**Enhancement:**
- `StabilityControl` baru (ESC): deteksi oversteer/understeer lewat perbandingan `forwardSpeed` vs `lateralAcceleration`, kemudian cut throttle dan selectively brake satu roda
- `RolloverWarning`: deteksi kemiringan kendaraan > threshold → notifikasi
- Anti-rollback di tanjakan (sudah ada di `EngineModel` tapi akan diekspos lebih jelas di sini)

```cpp
class DriveAssistController {
public:
    struct AssistState {
        bool tcsActive      = false;
        bool absActive      = false;
        bool escActive      = false;  // ← baru
        bool lcArmed        = false;
        bool rollWarning    = false;  // ← baru
        float tcsThrottle   = 1.0f;
        float absBrake      = 1.0f;
        float escBrake      = 0.0f;  // ← baru
    };

    void Update(Vehicle veh, VehicleData& data, int gear,
                float clutch, float& throttle, float& brake,
                float forwardSpeed, bool engineOn, bool automaticMode);
    void Reset();

    const AssistState& GetState() const;

private:
    AssistState m_state;
};
```

---

### Controller 6: `VehicleSessionController`
**File:** `src/Script/Controllers/VehicleSessionController.h/.cpp`

Koordinasi saat player ganti mobil. Ngurusin:
- Deteksi vehicle change
- Reset semua controller secara terurut
- Penerapan initial state (upgrade check, notification, grace period)

```cpp
class VehicleSessionController {
public:
    bool CheckAndUpdate(Vehicle current);
    bool IsInGracePeriod() const;
    bool JustChanged()     const;
    void Reset();

    Vehicle GetActiveVehicle() const;

private:
    Vehicle   m_activeVehicle   = 0;
    ULONGLONG m_enterTick       = 0;
    bool      m_justChanged     = false;
    bool      m_notifyShown     = false;
};
```

---

### Controller 7: `DiagnosticsController` ← **SISTEM BARU**
**File:** `src/Script/Controllers/DiagnosticsController.h/.cpp`

Sebelumnya 80+ baris `LOG_INFO` status berceceran di dalam loop utama. Controller ini membungkus semua logging diagnostik **terstruktur** + manajemen telemetry session.

**Enhancement:**
- Structured per-frame status dump (1x/detik) → tetap sama tapi dikemas rapi
- `TelemetrySession`: otomatis start/stop berdasar kondisi (misalnya: gear > 2, speed > 40 km/h)
- `FaultRegistry`: nyimpen daftar fault/error unik yang pernah terjadi dalam sesi ini (misalnya: "money_shift", "clutch_overheat"), bisa di-query dari menu

```cpp
class DiagnosticsController {
public:
    void Update(Vehicle veh, VehicleData& data,
                const TransmissionController& trans,
                const EngineController& engine,
                const DriveAssistController& assist,
                const AutomaticGearbox::State& autoState);
    void RecordFault(const char* faultCode);   // ← baru
    void Reset();

    const std::vector<std::string>& GetFaults() const; // ← baru

private:
    DWORD m_lastStatusLog = 0;
    std::vector<std::string> m_faultLog;   // ← baru
};
```

---

### Controller 8: `HUDController`
**File:** `src/Script/Controllers/HUDController.h/.cpp`

Ambil dari baris 974–1074. Ngurusin semua render:
- Gear HUD, Speedometer cluster, overlay bars, debug overlay, warning labels
- **Enhancement:** Warning label system lebih modular (bisa lu tambah/kurangi label dari config tanpa edit kode)

```cpp
class HUDController {
public:
    void Update(Vehicle veh, VehicleData& data,
                VehicleProfile::Drivetrain profile,
                const TransmissionController& trans,
                const EngineController& engine,
                const DriveAssistController& assist,
                float speedKmH);
};
```

---

## Bagian 2 — Enhancement Sistem Yang Sudah Ada

### 2.1 `EngineModel` — Torque Curve Per Vehicle Class
**File:** `src/Vehicle/Engine/EngineModel.cpp`

Saat ini `ResolvePhysicalRPMRange()` punya 4 kondisi hard-coded berdasarkan vehicle class integer. 

**Enhancement:** Ganti dengan tabel `VehicleCharacteristic` yang lebih ekspresif:
- Diesel: torque curve flat di low RPM, redline rendah
- Petrol Sport: peak torque di mid-high RPM
- Motorcycle: redline tinggi, idle rendah
- Heavy Truck: massive low-end torque, low redline

```cpp
// Baru di EngineModel.cpp
struct VehicleCharacteristic {
    float idleRPM;
    float redlineRPM;
    float peakTorqueBand;   // 0.0-1.0 normalized RPM dimana torque peak
    float torqueFlatness;   // seberapa flat kurva torsinya (0=peaky, 1=flat)
};
static VehicleCharacteristic ResolveCharacteristic(Vehicle vehicle, VehicleProfile::Drivetrain profile);
```

### 2.2 `ClutchSystem` — Clutch Wear & Plate Condition
**File:** `src/Vehicle/Clutch/ClutchSystem.h/.cpp`

Saat ini `ClutchSystem` punya `heat` tapi plate condition-nya nggak pernah *degradasi permanen*. Setiap reset (ganti mobil), kondisi kopling balik 100%.

**Enhancement:** Tambah `plateWear` (0.0–1.0) yang bertambah perlahan berdasarkan heat history. `plateWear` tinggi → `torqueCapacity` turun → slip lebih mudah terjadi → butuh servis di bengkel.

```cpp
// Tambah di State struct:
float plateWear = 0.0f;       // ← akumulasi, tidak di-reset saat ganti mobil
float wearRate  = 0.0f;       // ← logging
```

### 2.3 `GearboxSystem` — Synchro Wear Persistence
**File:** `src/Vehicle/Gearbox/Core/GearboxSystem.h/.cpp`

Mirip dengan clutch wear, `synchroWear[]` saat ini di-reset tiap ganti mobil. Padahal harusnya nyimpen kondisi per-kendaraan.

**Enhancement:** Tambah `GearboxSystem::SaveWear(vehicleHash)` dan `LoadWear(vehicleHash)` yang nyimpen ke INI. Kalau gearbox cukup aus → opsi service di `ServiceInteraction`.

### 2.4 `AudioEngine` — Kontekstual Lebih Kaya
**File:** `src/Audio/AudioEngine.cpp`

**Enhancement:** Tambah beberapa trigger audio yang saat ini belum ada:
- `PlayEngineStart()` — suara starter motor / ignition click
- `PlayEngineStall()` — suara mesin mati
- `PlayTurboWhistle()` — suara turbo spool (kalau `TurboSystem::GetBoostPressure() > 0.3f`)
- `PlayClutchSlip()` — suara selip kopling (rate tergantung heat)

### 2.5 `ModLogger` — Structured Severity + In-Game Log Viewer
**File:** `src/Core/ModLogger.h/.cpp`

**Enhancement:** Tambah `ModLogger::GetRecentLines(int count)` yang bisa dipanggil oleh menu untuk menampilkan log terbaru di dalam game (kayak F8 console tapi di dalam GTA).

---

## Bagian 3 — Sistem Brand New

### 3.1 `VersionInfo` — Version Manifest
**File:** `src/Core/VersionInfo.h`

Ganti semua string "r25" yang hardcoded di `MainScript.cpp` dengan satu sumber kebenaran:

```cpp
namespace VersionInfo {
    constexpr const char* kVersion     = "1.1.0";
    constexpr const char* kBuildLabel  = "Melar Transmission";
    constexpr const char* kReleaseDate = "2026";
    constexpr bool        kReleaseBuild = true;
}
```

Semua notifikasi, log, dan header akan pakai ini.

### 3.2 `DrivingEventBus` — Event System Antar-Modul
**File:** `src/Script/DrivingEventBus.h/.cpp`

Saat ini modul-modul berkomunikasi lewat polling (tiap frame ngecek state). Dengan Event Bus, modul bisa **publish event** dan modul lain **subscribe** tanpa perlu tahu satu sama lain.

```cpp
namespace DrivingEventBus {
    enum class Event {
        GearShiftUp,
        GearShiftDown,
        ClutchDump,
        EngineStall,
        TurboBlowoff,
        MoneyShift,
        ABSActivated,
        TCSActivated,
    };

    void Publish(Event e);
    void Subscribe(Event e, std::function<void()> callback);
    void FlushFrame(); // dipanggil di awal tiap frame
}
```

Contoh penggunaan: `AudioEngine` subscribe ke `GearShiftUp` → play suara shift otomatis, tanpa `MainScript` perlu pasang kabelnya manual.

### 3.3 `VehicleBlackboard` — Shared Frame State
**File:** `src/Script/VehicleBlackboard.h`

Saat ini banyak nilai dihitung berkali-kali (speed, RPM, maxGear) dan dioper-oper lewat parameter fungsi. `VehicleBlackboard` adalah struct sederhana yang di-populate di awal setiap frame dan bisa dibaca oleh siapa saja tanpa parameter.

```cpp
struct VehicleBlackboard {
    Vehicle vehicle        = 0;
    int     maxGear        = 0;
    float   speedKmH       = 0.0f;
    float   forwardSpeed   = 0.0f;
    float   rpm            = 0.0f;
    bool    actualEngineOn = false;
    VehicleProfile::Drivetrain profile = VehicleProfile::Drivetrain::Standard;
    // ... dll
};

extern VehicleBlackboard g_frame; // diisi di awal loop, dibaca oleh semua controller
```

---

## Rencana Versi String: r25 → v1.1

Ganti semua kemunculan `"r25"` dan `"r18"` (ada di trace logs) ke:
- Notifikasi startup: `"Melar Transmission v1.1"`
- Trace log: `"TRACE v1 stage=..."`

---

## Bagian 4 — Super Dynamic Gearbox Overhaul (Tahap Lanjut)

Untuk membuat setiap mobil memiliki "soul" yang berbeda, sistem Gearbox dan Fisika akan dioverhaul menjadi *Super Dynamic*:

### 4.1. Integrasi Native Handling Data
Kita akan baca parameter asli dari `handling.meta` setiap kendaraan:
- `fInitialDriveForce` & `fInitialDriveMaxFlatVel`: Untuk menentukan curve power yang lebih presisi.
- `fClutchChangeRateScaleUpShift` & `fDownShift`: Mobil yang aslinya gesit pindah gigi akan memiliki profil sinkronisasi dan shift-delay yang lebih singkat di mod. Mobil tua atau truk akan terasa sangat lambat (sluggish).

### 4.2. Customizable Gearbox Profile (ala IKT)
Kita tambahkan file konfigurasi `gearing.ini` terpisah:
- **Custom Ratios:** Player bisa define custom gear ratio per kendaraan (berdasar model name/hash).
- **Dynamic Class-Based Ratios:** Jika tidak ada custom ratio, sistem akan men-generate ratio berdasarkan kelas (Misal: SUV pakai close-ratio bawah, Sport pakai close-ratio atas, Truk pakai crawler gear).
- **Final Drive Tuning:** Opsi untuk ngubah final drive ratio tanpa harus ngubah per-gigi.

### 4.3. Modifikasi Berdampak Dinamis (LSC Upgrades)
Upgrade di Los Santos Customs nggak cuma nambah top speed, tapi ngerubah behaviour mekanik:
- **Transmission Upgrade:** 
  - *Standard/Street:* Synchro gearbox, butuh kopling.
  - *Race Transmission:* Berubah jadi **Dog-box transmission**. Shift instant, tidak butuh kopling untuk upshift (powershift), suara shift lebih kasar (*clunk!*).
- **Engine Upgrade:** Redline limit naik, idle sedikit tidak stabil (hunting RPM) karena camshaft agresif.
- **Turbo Upgrade:** Respons lag turbo (boost build-up) dipengaruhi tipe mobil.

---

## Urutan Eksekusi (Bertahap)

| Tahap | Isi | Status |
|-------|-----|--------|
| **1** | `VersionInfo.h` + `VehicleBlackboard.h` + ganti semua "r25" | ✅ Selesai |
| **2** | `VehicleSessionController` + `EngineController` + `SignalController` | ✅ Selesai |
| **3** | `CalibrationController` + `HUDController` | ✅ Selesai |
| **4** | `TransmissionController` (core physics) + Creep Fix | ✅ Selesai |
| **5** | Refactor `MainScript.cpp` jadi Orchestrator murni | ✅ Selesai |
| **6** | `DriveAssistController`: TCS/ABS/LC + ESC/Rollover | ✅ Selesai |
| **7** | `DiagnosticsController` + payload `DrivingEventBus` + audio wiring | ✅ Selesai |
| **8** | Native-handling dynamic gearbox + profile/ratio architecture | ✅ Selesai |
| **9** | LSC tuning, dynamic cluster, audio, weight/flex physics | ✅ Selesai |
| **10** | Persistensi plate/synchro wear lintas sesi | 🧭 Follow-up |

## Update v1.1 — Implementasi Aktual

- `src/Vehicle/Gearbox/` sudah dipisah menjadi `Automatic/`, `Manual/`, dan
  `Core/`. Profil membaca `fInitialDriveForce`,
  `fInitialDriveMaxFlatVel`, serta clutch change rate native. Ratio custom
  hanya ditulis jika layout inline per-kendaraan tervalidasi.
- Upgrade transmission LSC dan paket Melar per model digabungkan. Paket Race
  membuka quickshifter dan powershifter pada mobil maupun motor dengan
  multiplier wear/health `0.0000001`, tanpa mengubah redline atau napas rasio
  native.
- Workshop LSC memiliki enam pedal map, empat clutch, empat flywheel, empat
  transmission calibration, creep, cruise, drivetrain mounts, serta kalibrasi
  TCS/ABS/launch. Semua pilihan disimpan berdasarkan model hash.
- `DriveAssistController` menjadi satu-satunya owner update TCS, ABS, launch,
  ESC, dan rollover. TCS/ABS memakai telemetry roda tervalidasi dan selalu
  fail-open bila layout tidak tersedia.
- `VehicleDynamics` mengestimasi massa berdasarkan kelas/dimensi, memodelkan
  compliance mount/driveline, torque wind-up, dan load-transfer pitch dengan
  force entity konservatif.
- Speedometer assetless berada di `src/UI/Speedometer/`, dengan sembilan
  layout aktual plus Auto Dynamic untuk mobil dan motor secara terpisah.
- Audio memakai event bus dan bank opsional untuk blow-off/flutter, clutch
  slip, transmission clunk, engine lug, ABS, TCS, launch, serta drivetrain
  flex. Jika WAV tidak tersedia, layer native yang aman dipakai bila aktif.

---

## Verification Plan

### Automated

- Audit delimiter source, whitespace diff, dan XML MSBuild.
- Cocokkan seluruh `.cpp/.h` di `src/` dengan `.vcxproj` dan `.filters`.
- Build `Release | x64` dilakukan di Visual Studio Windows setelah
  push/pull; audit macOS ini sengaja tidak menghasilkan binary.

### Manual (In-Game)
1. Mod startup → notifikasi `"Melar Transmission v1.1"` muncul.
2. Cold start — starter fatigue masih jalan (mesin makin susah dinyalain kalau sering gagal).
3. Lampu sein (kiri/kanan/hazard) + auto-cancel saat belok.
4. Mode switch Manual → Automatic → Off, notifikasi muncul tiap switch.
5. Pindah gigi manual — suara shift, grind, speedometer HUD jalan.
6. Selip kopling + panas → suara slip, heat bar naik di overlay.
7. TCS aktif → throttle terpotong, label `TCS` muncul di HUD.
8. ABS aktif → brake pressure terpotong, label `ABS` muncul.
9. Kehabisan bensin → mesin mati + notifikasi.
10. Ganti kendaraan → semua state reset, re-kalibrate kalau perlu.
