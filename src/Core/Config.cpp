// =============================================================================
// Config.cpp
//
// Smoothing values (ThrottleAttack, etc.) are now time constants τ in SECONDS:
//   τ = 0.10 → reaches 63 % of target in 100 ms (independent of frame rate)
//   τ = 0.25 → reaches 63 % of target in 250 ms
// The old code used additive per-frame steps, which behaved differently at
// 30 Hz vs 60 Hz vs 120 Hz.  These τ values give the same feel at any frame rate.
// =============================================================================
#include "Config.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace Config {

int TransmissionMode = 2;

// ── Digital keys ──────────────────────────────────────────────────────────────
int KeyShiftUp      = VK_LSHIFT;
int KeyShiftDown    = VK_LCONTROL;
int KeyClutch       = 0x58;       // X
int KeyEngine       = 0x5A;       // Z
int KeyMenu         = 0xDB;       // [
int KeySignalLeft   = VK_NUMPAD4;
int KeySignalRight  = VK_NUMPAD6;
int KeySignalHazard = 0x48;       // H
int KeyParkingBrake = 0x50;       // P
int KeyRefuel       = 0x45;       // E
int KeyOilService   = 0x4F;       // O
int KeyWorkshop     = 0x45;       // E

bool SignalAutoCancelSteer = true;

// ── Feature flags ─────────────────────────────────────────────────────────────
bool DebugOverlay    = true;
bool AllowQuadbikes  = true;
bool UseRealClutch   = true;
bool RequireColdStart = true;
bool ForceRecalibrate = false;
bool LaunchControl = false;
float LaunchControlRPM = 0.72f;
bool TcsEnabled = true;
float TcsSlipTarget = 0.12f;
float TcsMaxCut = 0.65f;
bool AbsEnabled = true;
float AbsSlipTarget = 0.16f;
float AbsMaxRelease = 0.70f;

bool IdleCreep = true;
float IdleCreepThrottle = 0.14f;
bool StallEnabled = true;
float StallRate = 1.20f;
float StallClutchThreshold = 0.65f;
float IdleTorqueFraction = 0.18f;
float LugStallRPM = 1500.0f;
float StallCutoffRPM = 950.0f;
float LugStallDelay = 2.20f;
float WaterStallDelay = 2.50f;
float RolloverStallDelay = 7.00f;
float RevHangDuration = 0.50f;
bool HardBrakeStall = true;
bool StarterInterlock = true;
bool AutomaticStartRequiresBrake = true;

float ClutchBiteStart = 0.18f;
float ClutchBiteEnd = 0.43f;
float ClutchHeatRate = 0.08f;
float ClutchCoolRate = 0.035f;
float ClutchFadeStart = 0.85f;
float ClutchFadeStrength = 0.45f;
float MaxClutchTorque = 1.00f;
bool ClutchJudder = true;

bool GearClash = true;
float GearGrindDamage = 0.04f;
float ShiftShockStrength = 0.65f;
float NoLiftShiftPenalty = 0.35f;
bool SynchronizerWear = true;
bool ShiftResistance = true;
bool NativeGearboxPatch = true;
bool FuelCutoffEngineBrake = true;
float ConnectedRPMSync = 0.25f;
bool AutomaticBrakeInterlock = true;
float AutomaticShiftDelay = 0.35f;
float AutomaticDUpRPM = 0.50f;
float AutomaticDDownRPM = 0.22f;
float AutomaticSUpRPM = 0.84f;
float AutomaticSDownRPM = 0.34f;
float AutomaticKickdownThrottle = 0.72f;
float AutomaticSTorqueBoost = 0.10f;
float AutomaticDKeyboardThrottle = 1.00f;
float AutomaticKickdownDelay = 0.65f;
bool AutomaticTCC = true;
bool AutomaticFluidOverheat = true;
bool AutomaticNeutralDropDamage = true;
bool AutomaticBrakeBoostStall = true;
float AutomaticThrottleAttack = 0.18f;
float AutomaticThrottleRelease = 0.32f;
float AutomaticBrakeAttack = 0.10f;
float AutomaticBrakeRelease = 0.20f;
bool BrakeThrottleOverride = true;
float BrakeOverrideDelay = 0.20f;
float BrakeOverrideCut = 0.85f;
float ReverseLockoutSpeedKmH = 5.0f;
float OverRevShiftDamage = 0.12f;
float ClutchDumpRate = 8.0f;
float ClutchDumpShock = 0.70f;
bool BrakeFadeEnabled = true;
float BrakeHeatRate = 0.018f;
float BrakeCoolRate = 0.035f;
float BrakeFadeStart = 0.78f;
float BrakeFadeStrength = 0.45f;

bool AudioEnabled = true;
float AudioMasterVolume = 0.72f;
float AudioPitchRandomness = 0.045f;
float AudioLimiterCeiling = 0.72f;
bool AudioNativeLayers = true;

bool FuelEnabled = true;
bool FuelBlipsEnabled = true;
float RefuelRatePerSecond = 0.035f;
bool MaintenanceEnabled = true;
float OilWearMultiplier = 1.0f;

// ── Analog smoothing — τ in seconds ──────────────────────────────────────────
// Recommended defaults for keyboard play:
//   Throttle: attack fast (0.08), release slow (0.28) — coasting feel
//   Brake:    attack medium-fast (0.07), release medium (0.18)
//   Clutch:   attack very fast (0.045), release fast (0.06) — snappy
//   Steer:    attack fast (0.055), release moderate (0.10) — responsive
float ThrottleAttack  = 0.40f;
float ThrottleRelease = 0.60f;
float BrakeAttack     = 0.30f;
float BrakeRelease    = 0.50f;
float ClutchAttack    = 0.20f;
float ClutchRelease   = 0.06f;

// Steering
float SteerAttack      = 0.15f;
float SteerRelease     = 0.30f;
float SteerExpo        = 0.50f;
float SteerDeadzonePct = 0.05f;

// Output expo curves (0=linear, 1=full cubic)
float ThrottleExpo = 0.45f;
float BrakeExpo    = 0.35f;
float ClutchExpo   = 0.25f;
int PedalPreset    = 0;

// ── Excluded vehicle classes ──────────────────────────────────────────────────
std::vector<int> ExcludedVehicleClasses;

// ── Overlay ───────────────────────────────────────────────────────────────────
bool  OverlayBars      = true;
bool  GearHudEnabled   = true;
bool  SpeedometerEnabled = true;
int   SpeedometerCarStyle = 0;
int   SpeedometerBikeStyle = 0;
int   SpeedometerUnits = 0;
int   SpeedometerAccent = 0;
bool  SpeedometerDetailed = true;
float SpeedometerPosX = 0.815f;
float SpeedometerPosY = 0.790f;
float SpeedometerScale = 1.00f;
float SpeedometerOpacity = 0.92f;
float OverlayPosX      = 0.02f;
float OverlayPosY      = 0.22f;
float OverlayBarWidth  = 0.12f;
float OverlayBarHeight = 0.014f;
float GearHudPosX      = 0.90f;
float GearHudPosY      = 0.20f;
float GearHudScale     = 1.00f;
float MenuPosX         = 0.695f;
float MenuPosY         = 0.105f;
float MenuScale        = 1.00f;
bool  WorkshopEnabled  = true;
float WorkshopRadius   = 14.0f;

void ApplyPedalPreset(int preset) {
    PedalPreset = std::clamp(preset, 0, 4);
    switch (PedalPreset) {
    case 0:
        ThrottleAttack = 0.08f;
        ThrottleRelease = 0.28f;
        BrakeAttack = 0.07f;
        BrakeRelease = 0.18f;
        ClutchAttack = 0.045f;
        ClutchRelease = 0.06f;
        ThrottleExpo = 0.25f;
        BrakeExpo = 0.20f;
        ClutchExpo = 0.10f;
        break;
    case 1:
        ThrottleAttack = 0.04f;
        ThrottleRelease = 0.14f;
        BrakeAttack = 0.035f;
        BrakeRelease = 0.12f;
        ClutchAttack = 0.025f;
        ClutchRelease = 0.045f;
        ThrottleExpo = 0.12f;
        BrakeExpo = 0.10f;
        ClutchExpo = 0.05f;
        break;
    case 2:
        ThrottleAttack = 0.14f;
        ThrottleRelease = 0.34f;
        BrakeAttack = 0.12f;
        BrakeRelease = 0.28f;
        ClutchAttack = 0.06f;
        ClutchRelease = 0.10f;
        ThrottleExpo = 0.35f;
        BrakeExpo = 0.28f;
        ClutchExpo = 0.18f;
        break;
    case 3:
        ThrottleAttack = 0.055f;
        ThrottleRelease = 0.18f;
        BrakeAttack = 0.045f;
        BrakeRelease = 0.15f;
        ClutchAttack = 0.035f;
        ClutchRelease = 0.075f;
        ThrottleExpo = 0.50f;
        BrakeExpo = 0.42f;
        ClutchExpo = 0.30f;
        break;
    default:
        break;
    }
}

void ResetPedalsToDefault() {
    ApplyPedalPreset(0);
}

// =============================================================================
namespace {

float ReadFloat(const char* section, const char* key, float fallback, const char* iniPath) {
    char buffer[32]{};
    if (!GetPrivateProfileStringA(section, key, "", buffer, sizeof(buffer), iniPath))
        return fallback;
    char* end = nullptr;
    const float v = std::strtof(buffer, &end);
    return (end == buffer) ? fallback : v;
}

std::vector<int> ParseIntList(const char* text) {
    std::vector<int> out;
    const char* cursor = text;
    while (*cursor) {
        char* end = nullptr;
        const long v = std::strtol(cursor, &end, 10);
        if (end == cursor) break;
        out.push_back(static_cast<int>(v));
        cursor = end;
        while (*cursor == ',' || *cursor == ' ') ++cursor;
    }
    return out;
}

bool BuildIniPath(HMODULE module, char (&out)[MAX_PATH],
                  const char *fileName = "melar-transmission.ini") {
    DWORD len = GetModuleFileNameA(module, out, MAX_PATH);
    if (!len || len >= MAX_PATH) return false;
    char* slash = std::strrchr(out, '\\');
    if (!slash) slash = std::strrchr(out, '/');
    if (!slash) return false;
    *slash = '\0';
    return strcat_s(out, "\\") == 0 && strcat_s(out, fileName) == 0;
}

void WriteInt(const char* section, const char* key, int value, const char* iniPath) {
    char buf[32]{};
    sprintf_s(buf, "%d", value);
    WritePrivateProfileStringA(section, key, buf, iniPath);
}

} // namespace

// =============================================================================
void WriteFloat(const char* section, const char* key, float value, const char* iniPath) {
    char buf[32]{};
    sprintf_s(buf, "%.4f", value);
    WritePrivateProfileStringA(section, key, buf, iniPath);
}

void ReadConfig(HMODULE module) {
    char ini[MAX_PATH]{};
    if (!BuildIniPath(module, ini)) return;
    bool migratedFromLegacy = false;
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES) {
        char legacyIni[MAX_PATH]{};
        if (BuildIniPath(module, legacyIni, "manual-trans.ini") &&
            GetFileAttributesA(legacyIni) != INVALID_FILE_ATTRIBUTES) {
            strcpy_s(ini, legacyIni);
            migratedFromLegacy = true;
        }
    }
    const int drivetrainSchema =
        GetPrivateProfileIntA("Internal", "DrivetrainSchema", 0, ini);

    TransmissionMode = std::clamp(
        static_cast<int>(
            GetPrivateProfileIntA("Transmission", "Mode", 2, ini)),
        0, 2);

    // Controls
    KeyShiftUp      = GetPrivateProfileIntA("Controls", "ShiftUp",      VK_LSHIFT,  ini);
    KeyShiftDown    = GetPrivateProfileIntA("Controls", "ShiftDown",    VK_LCONTROL,ini);
    KeyClutch       = GetPrivateProfileIntA("Controls", "ClutchKey",    0x58,        ini);
    KeyEngine       = GetPrivateProfileIntA("Controls", "EngineKey",    0x5A,        ini);
    KeyMenu         = GetPrivateProfileIntA("Controls", "MenuKey",      0xDB,        ini);
    KeySignalLeft   = GetPrivateProfileIntA("Controls", "SignalLeft",   VK_NUMPAD4, ini);
    KeySignalRight  = GetPrivateProfileIntA("Controls", "SignalRight",  VK_NUMPAD6, ini);
    KeySignalHazard = GetPrivateProfileIntA("Controls", "SignalHazard", 0x48,        ini); // H
    KeyParkingBrake = GetPrivateProfileIntA("Controls", "ParkingBrake",0x50,        ini);
    KeyRefuel       = GetPrivateProfileIntA("Controls", "Refuel",      0x45,        ini);
    KeyOilService   = GetPrivateProfileIntA("Controls", "OilService",  0x4F,        ini);
    KeyWorkshop     = GetPrivateProfileIntA("Controls", "Workshop",    0x45,        ini);

    SignalAutoCancelSteer = GetPrivateProfileIntA("Controls","SignalAutoCancelSteer",1,ini) != 0;

    // Features
    DebugOverlay     = GetPrivateProfileIntA("Debug",    "Overlay",         1, ini) != 0;
    AllowQuadbikes   = GetPrivateProfileIntA("Vehicles", "AllowQuadbikes",  1, ini) != 0;
    UseRealClutch    = GetPrivateProfileIntA("Vehicles", "UseRealClutch",   1, ini) != 0;
    RequireColdStart = GetPrivateProfileIntA("Vehicles", "RequireColdStart",1, ini) != 0;
    LaunchControl = GetPrivateProfileIntA("Engine", "LaunchControl", 0, ini) != 0;
    LaunchControlRPM = ReadFloat("Engine", "LaunchControlRPM", 0.72f, ini);
    IdleCreep = GetPrivateProfileIntA("Engine", "IdleCreep", 1, ini) != 0;
    IdleCreepThrottle = ReadFloat("Engine", "IdleCreepThrottle", 0.14f, ini);
    StallEnabled = GetPrivateProfileIntA("Engine", "StallEnabled", 1, ini) != 0;
    StallRate = ReadFloat("Engine", "StallRate", 1.20f, ini);
    StallClutchThreshold =
        ReadFloat("Engine", "StallClutchThreshold", 0.65f, ini);
    IdleTorqueFraction =
        ReadFloat("Engine", "IdleTorqueFraction", 0.18f, ini);
    LugStallRPM =
        ReadFloat("Engine", "LugStallRPM", 1500.0f, ini);
    StallCutoffRPM =
        std::clamp(ReadFloat("Engine", "StallCutoffRPM", 950.0f, ini),
                   450.0f, 1800.0f);
    LugStallDelay =
        ReadFloat("Engine", "LugStallDelay", 2.20f, ini);
    WaterStallDelay =
        ReadFloat("Engine", "WaterStallDelay", 2.50f, ini);
    RolloverStallDelay =
        ReadFloat("Engine", "RolloverStallDelay", 7.00f, ini);
    RevHangDuration =
        ReadFloat("Engine", "RevHangDuration", 0.50f, ini);
    HardBrakeStall =
        GetPrivateProfileIntA("Engine", "HardBrakeStall", 1, ini) != 0;
    StarterInterlock =
        GetPrivateProfileIntA("Engine", "StarterInterlock", 1, ini) != 0;
    AutomaticStartRequiresBrake =
        GetPrivateProfileIntA("Engine", "AutomaticStartRequiresBrake", 1,
                              ini) != 0;
    ConnectedRPMSync =
        ReadFloat("Engine", "ConnectedRPMSync", 0.25f, ini);

    AutomaticBrakeInterlock =
        GetPrivateProfileIntA("Automatic", "BrakeInterlock", 1, ini) != 0;
    AutomaticShiftDelay =
        ReadFloat("Automatic", "ShiftDelay", 0.35f, ini);
    AutomaticDUpRPM = ReadFloat("Automatic", "DUpRPM", 0.50f, ini);
    AutomaticDDownRPM = ReadFloat("Automatic", "DDownRPM", 0.22f, ini);
    AutomaticSUpRPM = ReadFloat("Automatic", "SUpRPM", 0.84f, ini);
    AutomaticSDownRPM = ReadFloat("Automatic", "SDownRPM", 0.34f, ini);
    if (drivetrainSchema < 5 &&
        std::fabs(AutomaticDUpRPM - 0.68f) < 0.001f &&
        std::fabs(AutomaticDDownRPM - 0.28f) < 0.001f &&
        std::fabs(AutomaticSUpRPM - 0.90f) < 0.001f &&
        std::fabs(AutomaticSDownRPM - 0.42f) < 0.001f) {
        AutomaticDUpRPM = 0.50f;
        AutomaticDDownRPM = 0.22f;
        AutomaticSUpRPM = 0.84f;
        AutomaticSDownRPM = 0.34f;
    }
    AutomaticKickdownThrottle =
        ReadFloat("Automatic", "KickdownThrottle", 0.72f, ini);
    AutomaticSTorqueBoost =
        ReadFloat("Automatic", "SportTorqueBoost", 0.10f, ini);
    AutomaticDKeyboardThrottle =
        ReadFloat("Automatic", "DKeyboardThrottle", 1.00f, ini);
    if (drivetrainSchema < 8)
        AutomaticDKeyboardThrottle = 1.00f;
    AutomaticKickdownDelay =
        ReadFloat("Automatic", "KickdownDelay", 0.65f, ini);
    AutomaticTCC =
        GetPrivateProfileIntA("Automatic", "TCC", 1, ini) != 0;
    AutomaticFluidOverheat =
        GetPrivateProfileIntA("Automatic", "FluidOverheat", 1, ini) != 0;
    AutomaticNeutralDropDamage =
        GetPrivateProfileIntA("Automatic", "NeutralDropDamage", 1, ini) != 0;
    AutomaticBrakeBoostStall =
        GetPrivateProfileIntA("Automatic", "BrakeBoostStall", 1, ini) != 0;
    AutomaticThrottleAttack =
        ReadFloat("Automatic", "ThrottleAttack", 0.18f, ini);
    AutomaticThrottleRelease =
        ReadFloat("Automatic", "ThrottleRelease", 0.32f, ini);
    AutomaticBrakeAttack =
        ReadFloat("Automatic", "BrakeAttack", 0.10f, ini);
    AutomaticBrakeRelease =
        ReadFloat("Automatic", "BrakeRelease", 0.20f, ini);

    BrakeThrottleOverride =
        GetPrivateProfileIntA("Pedals", "BrakeThrottleOverride", 1, ini) != 0;
    BrakeOverrideDelay =
        ReadFloat("Pedals", "BrakeOverrideDelay", 0.20f, ini);
    BrakeOverrideCut =
        ReadFloat("Pedals", "BrakeOverrideCut", 0.85f, ini);

    ReverseLockoutSpeedKmH =
        ReadFloat("Gearbox", "ReverseLockoutSpeedKmH", 5.0f, ini);
    OverRevShiftDamage =
        ReadFloat("Gearbox", "OverRevShiftDamage", 0.12f, ini);
    ClutchDumpRate = ReadFloat("Clutch", "DumpRate", 8.0f, ini);
    ClutchDumpShock = ReadFloat("Clutch", "DumpShock", 0.70f, ini);

    BrakeFadeEnabled =
        GetPrivateProfileIntA("Brakes", "FadeEnabled", 1, ini) != 0;
    BrakeHeatRate = ReadFloat("Brakes", "HeatRate", 0.018f, ini);
    BrakeCoolRate = ReadFloat("Brakes", "CoolRate", 0.035f, ini);
    BrakeFadeStart = ReadFloat("Brakes", "FadeStart", 0.78f, ini);
    BrakeFadeStrength =
        ReadFloat("Brakes", "FadeStrength", 0.45f, ini);

    AudioEnabled =
        GetPrivateProfileIntA("Audio", "Enabled", 1, ini) != 0;
    AudioMasterVolume =
        std::clamp(ReadFloat("Audio", "MasterVolume", 0.72f, ini), 0.0f, 1.0f);
    AudioPitchRandomness =
        std::clamp(ReadFloat("Audio", "PitchRandomness", 0.045f, ini),
                   0.0f, 0.18f);
    AudioLimiterCeiling =
        std::clamp(ReadFloat("Audio", "LimiterCeiling", 0.72f, ini),
                   0.25f, 0.95f);
    AudioNativeLayers =
        GetPrivateProfileIntA("Audio", "NativeLayers", 1, ini) != 0;

    FuelEnabled =
        GetPrivateProfileIntA("Maintenance", "FuelEnabled", 1, ini) != 0;
    FuelBlipsEnabled =
        GetPrivateProfileIntA("Maintenance", "FuelBlips", 1, ini) != 0;
    RefuelRatePerSecond =
        std::clamp(ReadFloat("Maintenance", "RefuelRatePerSecond", 0.035f,
                             ini),
                   0.005f, 0.25f);
    MaintenanceEnabled =
        GetPrivateProfileIntA("Maintenance", "Enabled", 1, ini) != 0;
    OilWearMultiplier =
        std::clamp(ReadFloat("Maintenance", "OilWearMultiplier", 1.0f, ini),
                   0.0f, 5.0f);

    TcsEnabled = GetPrivateProfileIntA("Assists", "TCS", 1, ini) != 0;
    TcsSlipTarget = ReadFloat("Assists", "TCSSlipTarget", 0.12f, ini);
    TcsMaxCut = ReadFloat("Assists", "TCSMaxCut", 0.65f, ini);
    AbsEnabled = GetPrivateProfileIntA("Assists", "ABS", 1, ini) != 0;
    AbsSlipTarget = ReadFloat("Assists", "ABSSlipTarget", 0.16f, ini);
    AbsMaxRelease = ReadFloat("Assists", "ABSMaxRelease", 0.70f, ini);

    GearClash = GetPrivateProfileIntA("Gearbox", "ClashEnabled", 1, ini) != 0;
    GearGrindDamage = ReadFloat("Gearbox", "GrindDamage", 0.04f, ini);
    ShiftShockStrength =
        ReadFloat("Gearbox", "ShiftShockStrength", 0.65f, ini);
    NoLiftShiftPenalty =
        ReadFloat("Gearbox", "NoLiftShiftPenalty", 0.35f, ini);
    SynchronizerWear =
        GetPrivateProfileIntA("Gearbox", "SynchronizerWear", 1, ini) != 0;
    ShiftResistance =
        GetPrivateProfileIntA("Gearbox", "ShiftResistance", 1, ini) != 0;
    NativeGearboxPatch =
        GetPrivateProfileIntA("Gearbox", "NativeOverridePatch", 1, ini) != 0;
    FuelCutoffEngineBrake =
        GetPrivateProfileIntA("Engine", "FuelCutoffEngineBrake", 1, ini) != 0;

    ClutchBiteStart = ReadFloat("Clutch", "BiteStart", 0.18f, ini);
    ClutchBiteEnd = ReadFloat("Clutch", "BiteEnd", 0.43f, ini);
    ClutchHeatRate = ReadFloat("Clutch", "HeatRate", 0.08f, ini);
    ClutchCoolRate = ReadFloat("Clutch", "CoolRate", 0.035f, ini);
    ClutchFadeStart = ReadFloat("Clutch", "FadeStart", 0.85f, ini);
    ClutchFadeStrength =
        ReadFloat("Clutch", "FadeStrength", 0.45f, ini);
    MaxClutchTorque =
        ReadFloat("Clutch", "MaxClutchTorque", 1.00f, ini);
    ClutchJudder =
        GetPrivateProfileIntA("Clutch", "Judder", 1, ini) != 0;

    // Analog smoothing τ (seconds)
    ThrottleAttack   = ReadFloat("Analog", "ThrottleAttack",   0.08f,  ini);
    ThrottleRelease  = ReadFloat("Analog", "ThrottleRelease",  0.28f,  ini);
    BrakeAttack      = ReadFloat("Analog", "BrakeAttack",      0.07f,  ini);
    BrakeRelease     = ReadFloat("Analog", "BrakeRelease",     0.18f,  ini);
    ClutchAttack     = ReadFloat("Analog", "ClutchAttack",     0.045f, ini);
    ClutchRelease    = ReadFloat("Analog", "ClutchRelease",    0.06f,  ini);

    // Expo
    ThrottleExpo     = ReadFloat("Analog", "ThrottleExpo",     0.25f,  ini);
    BrakeExpo        = ReadFloat("Analog", "BrakeExpo",        0.20f,  ini);
    ClutchExpo       = ReadFloat("Analog", "ClutchExpo",       0.10f,  ini);
    PedalPreset = std::clamp(
        static_cast<int>(GetPrivateProfileIntA("Analog", "Preset", 0, ini)),
        0, 4);

    // Steering
    SteerAttack      = ReadFloat("Steering", "Attack",      0.055f, ini);
    SteerRelease     = ReadFloat("Steering", "Release",     0.10f,  ini);
    SteerExpo        = ReadFloat("Steering", "Expo",        0.35f,  ini);
    SteerDeadzonePct = ReadFloat("Steering", "DeadzonePct", 0.05f,  ini);

    // Excluded classes
    char excludedBuf[128]{};
    GetPrivateProfileStringA("Vehicles", "ExcludedClasses", "", excludedBuf, sizeof(excludedBuf), ini);
    ExcludedVehicleClasses = ParseIntList(excludedBuf);

    // Overlay
    OverlayBars      = GetPrivateProfileIntA("Overlay", "Bars",  1,   ini) != 0;
    GearHudEnabled   = GetPrivateProfileIntA("Overlay", "GearHud", 1, ini) != 0;
    SpeedometerEnabled =
        GetPrivateProfileIntA("Speedometer", "Enabled", 1, ini) != 0;
    SpeedometerCarStyle = std::clamp(
        static_cast<int>(
            GetPrivateProfileIntA("Speedometer", "CarStyle", 0, ini)),
        0, 2);
    SpeedometerBikeStyle = std::clamp(
        static_cast<int>(
            GetPrivateProfileIntA("Speedometer", "BikeStyle", 0, ini)),
        0, 2);
    SpeedometerUnits = std::clamp(
        static_cast<int>(
            GetPrivateProfileIntA("Speedometer", "Units", 0, ini)),
        0, 1);
    SpeedometerAccent = std::clamp(
        static_cast<int>(
            GetPrivateProfileIntA("Speedometer", "Accent", 0, ini)),
        0, 4);
    SpeedometerDetailed =
        GetPrivateProfileIntA("Speedometer", "Detailed", 1, ini) != 0;
    SpeedometerPosX =
        std::clamp(ReadFloat("Speedometer", "PosX", 0.815f, ini),
                   0.12f, 0.92f);
    SpeedometerPosY =
        std::clamp(ReadFloat("Speedometer", "PosY", 0.790f, ini),
                   0.20f, 0.92f);
    SpeedometerScale =
        std::clamp(ReadFloat("Speedometer", "Scale", 1.0f, ini),
                   0.65f, 1.40f);
    SpeedometerOpacity =
        std::clamp(ReadFloat("Speedometer", "Opacity", 0.92f, ini),
                   0.35f, 1.0f);
    OverlayPosX      = ReadFloat("Overlay", "PosX",      0.02f,  ini);
    OverlayPosY      = ReadFloat("Overlay", "PosY",      0.22f,  ini);
    OverlayBarWidth  = ReadFloat("Overlay", "BarWidth",  0.12f,  ini);
    OverlayBarHeight = ReadFloat("Overlay", "BarHeight", 0.014f, ini);
    GearHudPosX      = std::clamp(ReadFloat("Overlay", "GearPosX", 0.90f, ini),
                                  0.05f, 0.95f);
    GearHudPosY      = std::clamp(ReadFloat("Overlay", "GearPosY", 0.20f, ini),
                                  0.08f, 0.90f);
    GearHudScale     = std::clamp(ReadFloat("Overlay", "GearScale", 1.0f, ini),
                                  0.65f, 1.50f);
    MenuPosX         = std::clamp(ReadFloat("Overlay", "MenuPosX", 0.695f, ini),
                                  0.00f, 0.78f);
    MenuPosY         = std::clamp(ReadFloat("Overlay", "MenuPosY", 0.105f, ini),
                                  0.02f, 0.55f);
    MenuScale        = std::clamp(ReadFloat("Overlay", "MenuScale", 1.0f, ini),
                                  0.70f, 1.20f);
    WorkshopEnabled =
        GetPrivateProfileIntA("Workshop", "Enabled", 1, ini) != 0;
    WorkshopRadius =
        std::clamp(ReadFloat("Workshop", "Radius", 14.0f, ini),
                   6.0f, 30.0f);
    if (drivetrainSchema < 10 && SpeedometerEnabled) {
        OverlayBars = false;
        GearHudEnabled = false;
    }
    // Pindahkan layout default lama yang numpuk minimap/notifikasi.
    if (std::fabs(OverlayPosX - 0.02f) < 0.001f &&
        std::fabs(OverlayPosY - 0.60f) < 0.001f) {
        OverlayPosY = 0.22f;
    }

    // Auto-create if the file doesn't exist yet
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES ||
        drivetrainSchema < 10 || migratedFromLegacy) {
        SaveConfig(module);
        char currentIni[MAX_PATH]{};
        if (BuildIniPath(module, currentIni))
            WriteInt("Internal", "DrivetrainSchema", 10, currentIni);
    }
}

void SaveConfig(HMODULE module) {
    char ini[MAX_PATH]{};
    if (!BuildIniPath(module, ini)) return;

    WriteInt("Transmission", "Mode", TransmissionMode, ini);

    WriteInt("Controls", "ShiftUp",              KeyShiftUp,      ini);
    WriteInt("Controls", "ShiftDown",            KeyShiftDown,    ini);
    WriteInt("Controls", "ClutchKey",            KeyClutch,       ini);
    WriteInt("Controls", "EngineKey",            KeyEngine,       ini);
    WriteInt("Controls", "MenuKey",              KeyMenu,         ini);
    WriteInt("Controls", "SignalLeft",           KeySignalLeft,   ini);
    WriteInt("Controls", "SignalRight",          KeySignalRight,  ini);
    WriteInt("Controls", "SignalHazard",         KeySignalHazard, ini);
    WriteInt("Controls", "ParkingBrake",         KeyParkingBrake, ini);
    WriteInt("Controls", "Refuel",               KeyRefuel,       ini);
    WriteInt("Controls", "OilService",           KeyOilService,   ini);
    WriteInt("Controls", "Workshop",             KeyWorkshop,     ini);
    WriteInt("Controls", "SignalAutoCancelSteer",SignalAutoCancelSteer ? 1 : 0, ini);

    WriteInt("Debug",    "Overlay",          DebugOverlay     ? 1 : 0, ini);
    WriteInt("Vehicles", "AllowQuadbikes",   AllowQuadbikes   ? 1 : 0, ini);
    WriteInt("Vehicles", "UseRealClutch",    UseRealClutch    ? 1 : 0, ini);
    WriteInt("Vehicles", "RequireColdStart", RequireColdStart ? 1 : 0, ini);
    WriteInt("Engine", "LaunchControl", LaunchControl ? 1 : 0, ini);
    WriteFloat("Engine", "LaunchControlRPM", LaunchControlRPM, ini);
    WriteInt("Engine", "IdleCreep", IdleCreep ? 1 : 0, ini);
    WriteFloat("Engine", "IdleCreepThrottle", IdleCreepThrottle, ini);
    WriteInt("Engine", "StallEnabled", StallEnabled ? 1 : 0, ini);
    WriteFloat("Engine", "StallRate", StallRate, ini);
    WriteFloat("Engine", "StallClutchThreshold", StallClutchThreshold, ini);
    WriteFloat("Engine", "IdleTorqueFraction", IdleTorqueFraction, ini);
    WriteFloat("Engine", "LugStallRPM", LugStallRPM, ini);
    WriteFloat("Engine", "StallCutoffRPM", StallCutoffRPM, ini);
    WriteFloat("Engine", "LugStallDelay", LugStallDelay, ini);
    WriteFloat("Engine", "WaterStallDelay", WaterStallDelay, ini);
    WriteFloat("Engine", "RolloverStallDelay", RolloverStallDelay, ini);
    WriteFloat("Engine", "RevHangDuration", RevHangDuration, ini);
    WriteInt("Engine", "HardBrakeStall", HardBrakeStall ? 1 : 0, ini);
    WriteInt("Engine", "FuelCutoffEngineBrake",
             FuelCutoffEngineBrake ? 1 : 0, ini);
    WriteInt("Engine", "StarterInterlock", StarterInterlock ? 1 : 0, ini);
    WriteInt("Engine", "AutomaticStartRequiresBrake",
             AutomaticStartRequiresBrake ? 1 : 0, ini);
    WriteFloat("Engine", "ConnectedRPMSync", ConnectedRPMSync, ini);

    WriteInt("Automatic", "BrakeInterlock",
             AutomaticBrakeInterlock ? 1 : 0, ini);
    WriteFloat("Automatic", "ShiftDelay", AutomaticShiftDelay, ini);
    WriteFloat("Automatic", "DUpRPM", AutomaticDUpRPM, ini);
    WriteFloat("Automatic", "DDownRPM", AutomaticDDownRPM, ini);
    WriteFloat("Automatic", "SUpRPM", AutomaticSUpRPM, ini);
    WriteFloat("Automatic", "SDownRPM", AutomaticSDownRPM, ini);
    WriteFloat("Automatic", "KickdownThrottle",
               AutomaticKickdownThrottle, ini);
    WriteFloat("Automatic", "SportTorqueBoost",
               AutomaticSTorqueBoost, ini);
    WriteFloat("Automatic", "DKeyboardThrottle",
               AutomaticDKeyboardThrottle, ini);
    WriteFloat("Automatic", "KickdownDelay", AutomaticKickdownDelay, ini);
    WriteInt("Automatic", "TCC", AutomaticTCC ? 1 : 0, ini);
    WriteInt("Automatic", "FluidOverheat",
             AutomaticFluidOverheat ? 1 : 0, ini);
    WriteInt("Automatic", "NeutralDropDamage",
             AutomaticNeutralDropDamage ? 1 : 0, ini);
    WriteInt("Automatic", "BrakeBoostStall",
             AutomaticBrakeBoostStall ? 1 : 0, ini);
    WriteFloat("Automatic", "ThrottleAttack", AutomaticThrottleAttack, ini);
    WriteFloat("Automatic", "ThrottleRelease", AutomaticThrottleRelease, ini);
    WriteFloat("Automatic", "BrakeAttack", AutomaticBrakeAttack, ini);
    WriteFloat("Automatic", "BrakeRelease", AutomaticBrakeRelease, ini);

    WriteInt("Pedals", "BrakeThrottleOverride",
             BrakeThrottleOverride ? 1 : 0, ini);
    WriteFloat("Pedals", "BrakeOverrideDelay", BrakeOverrideDelay, ini);
    WriteFloat("Pedals", "BrakeOverrideCut", BrakeOverrideCut, ini);

    WriteFloat("Gearbox", "ReverseLockoutSpeedKmH",
               ReverseLockoutSpeedKmH, ini);
    WriteFloat("Gearbox", "OverRevShiftDamage", OverRevShiftDamage, ini);
    WriteFloat("Clutch", "DumpRate", ClutchDumpRate, ini);
    WriteFloat("Clutch", "DumpShock", ClutchDumpShock, ini);

    WriteInt("Brakes", "FadeEnabled", BrakeFadeEnabled ? 1 : 0, ini);
    WriteFloat("Brakes", "HeatRate", BrakeHeatRate, ini);
    WriteFloat("Brakes", "CoolRate", BrakeCoolRate, ini);
    WriteFloat("Brakes", "FadeStart", BrakeFadeStart, ini);
    WriteFloat("Brakes", "FadeStrength", BrakeFadeStrength, ini);

    WriteInt("Audio", "Enabled", AudioEnabled ? 1 : 0, ini);
    WriteFloat("Audio", "MasterVolume", AudioMasterVolume, ini);
    WriteFloat("Audio", "PitchRandomness", AudioPitchRandomness, ini);
    WriteFloat("Audio", "LimiterCeiling", AudioLimiterCeiling, ini);
    WriteInt("Audio", "NativeLayers", AudioNativeLayers ? 1 : 0, ini);

    WriteInt("Maintenance", "FuelEnabled", FuelEnabled ? 1 : 0, ini);
    WriteInt("Maintenance", "FuelBlips",
             FuelBlipsEnabled ? 1 : 0, ini);
    WriteFloat("Maintenance", "RefuelRatePerSecond",
               RefuelRatePerSecond, ini);
    WriteInt("Maintenance", "Enabled", MaintenanceEnabled ? 1 : 0, ini);
    WriteFloat("Maintenance", "OilWearMultiplier",
               OilWearMultiplier, ini);

    WriteInt("Assists", "TCS", TcsEnabled ? 1 : 0, ini);
    WriteFloat("Assists", "TCSSlipTarget", TcsSlipTarget, ini);
    WriteFloat("Assists", "TCSMaxCut", TcsMaxCut, ini);
    WriteInt("Assists", "ABS", AbsEnabled ? 1 : 0, ini);
    WriteFloat("Assists", "ABSSlipTarget", AbsSlipTarget, ini);
    WriteFloat("Assists", "ABSMaxRelease", AbsMaxRelease, ini);

    WriteInt("Gearbox", "ClashEnabled", GearClash ? 1 : 0, ini);
    WriteFloat("Gearbox", "GrindDamage", GearGrindDamage, ini);
    WriteFloat("Gearbox", "ShiftShockStrength", ShiftShockStrength, ini);
    WriteFloat("Gearbox", "NoLiftShiftPenalty", NoLiftShiftPenalty, ini);
    WriteInt("Gearbox", "SynchronizerWear",
             SynchronizerWear ? 1 : 0, ini);
    WriteInt("Gearbox", "ShiftResistance", ShiftResistance ? 1 : 0, ini);
    WriteInt("Gearbox", "NativeOverridePatch",
             NativeGearboxPatch ? 1 : 0, ini);

    WriteFloat("Clutch", "BiteStart", ClutchBiteStart, ini);
    WriteFloat("Clutch", "BiteEnd", ClutchBiteEnd, ini);
    WriteFloat("Clutch", "HeatRate", ClutchHeatRate, ini);
    WriteFloat("Clutch", "CoolRate", ClutchCoolRate, ini);
    WriteFloat("Clutch", "FadeStart", ClutchFadeStart, ini);
    WriteFloat("Clutch", "FadeStrength", ClutchFadeStrength, ini);
    WriteFloat("Clutch", "MaxClutchTorque", MaxClutchTorque, ini);
    WriteInt("Clutch", "Judder", ClutchJudder ? 1 : 0, ini);

    WriteFloat("Analog", "ThrottleAttack",   ThrottleAttack,  ini);
    WriteFloat("Analog", "ThrottleRelease",  ThrottleRelease, ini);
    WriteFloat("Analog", "BrakeAttack",      BrakeAttack,     ini);
    WriteFloat("Analog", "BrakeRelease",     BrakeRelease,    ini);
    WriteFloat("Analog", "ClutchAttack",     ClutchAttack,    ini);
    WriteFloat("Analog", "ClutchRelease",    ClutchRelease,   ini);
    WriteFloat("Analog", "ThrottleExpo",     ThrottleExpo,    ini);
    WriteFloat("Analog", "BrakeExpo",        BrakeExpo,       ini);
    WriteFloat("Analog", "ClutchExpo",       ClutchExpo,      ini);
    WriteInt("Analog", "Preset", PedalPreset, ini);

    WriteFloat("Steering", "Attack",      SteerAttack,      ini);
    WriteFloat("Steering", "Release",     SteerRelease,     ini);
    WriteFloat("Steering", "Expo",        SteerExpo,        ini);
    WriteFloat("Steering", "DeadzonePct", SteerDeadzonePct, ini);

    WriteInt("Overlay", "Bars", OverlayBars ? 1 : 0, ini);
    WriteInt("Overlay", "GearHud", GearHudEnabled ? 1 : 0, ini);
    WriteInt("Speedometer", "Enabled", SpeedometerEnabled ? 1 : 0, ini);
    WriteInt("Speedometer", "CarStyle", SpeedometerCarStyle, ini);
    WriteInt("Speedometer", "BikeStyle", SpeedometerBikeStyle, ini);
    WriteInt("Speedometer", "Units", SpeedometerUnits, ini);
    WriteInt("Speedometer", "Accent", SpeedometerAccent, ini);
    WriteInt("Speedometer", "Detailed", SpeedometerDetailed ? 1 : 0, ini);
    WriteFloat("Speedometer", "PosX", SpeedometerPosX, ini);
    WriteFloat("Speedometer", "PosY", SpeedometerPosY, ini);
    WriteFloat("Speedometer", "Scale", SpeedometerScale, ini);
    WriteFloat("Speedometer", "Opacity", SpeedometerOpacity, ini);
    WriteFloat("Overlay", "PosX",      OverlayPosX,      ini);
    WriteFloat("Overlay", "PosY",      OverlayPosY,      ini);
    WriteFloat("Overlay", "BarWidth",  OverlayBarWidth,  ini);
    WriteFloat("Overlay", "BarHeight", OverlayBarHeight, ini);
    WriteFloat("Overlay", "GearPosX", GearHudPosX, ini);
    WriteFloat("Overlay", "GearPosY", GearHudPosY, ini);
    WriteFloat("Overlay", "GearScale", GearHudScale, ini);
    WriteFloat("Overlay", "MenuPosX", MenuPosX, ini);
    WriteFloat("Overlay", "MenuPosY", MenuPosY, ini);
    WriteFloat("Overlay", "MenuScale", MenuScale, ini);
    WriteInt("Workshop", "Enabled", WorkshopEnabled ? 1 : 0, ini);
    WriteFloat("Workshop", "Radius", WorkshopRadius, ini);
}

} // namespace Config
