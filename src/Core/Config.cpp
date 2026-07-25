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
float AutomaticDKeyboardThrottle = 0.62f;
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

// ── Excluded vehicle classes ──────────────────────────────────────────────────
std::vector<int> ExcludedVehicleClasses;

// ── Overlay ───────────────────────────────────────────────────────────────────
bool  OverlayBars      = true;
float OverlayPosX      = 0.02f;
float OverlayPosY      = 0.22f;
float OverlayBarWidth  = 0.12f;
float OverlayBarHeight = 0.014f;

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

bool BuildIniPath(HMODULE module, char (&out)[MAX_PATH]) {
    DWORD len = GetModuleFileNameA(module, out, MAX_PATH);
    if (!len || len >= MAX_PATH) return false;
    char* slash = std::strrchr(out, '\\');
    if (!slash) slash = std::strrchr(out, '/');
    if (!slash) return false;
    *slash = '\0';
    return strcat_s(out, "\\manual-trans.ini") == 0;
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
        ReadFloat("Automatic", "DKeyboardThrottle", 0.62f, ini);
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
    OverlayPosX      = ReadFloat("Overlay", "PosX",      0.02f,  ini);
    OverlayPosY      = ReadFloat("Overlay", "PosY",      0.22f,  ini);
    OverlayBarWidth  = ReadFloat("Overlay", "BarWidth",  0.12f,  ini);
    OverlayBarHeight = ReadFloat("Overlay", "BarHeight", 0.014f, ini);
    // Pindahkan layout default lama yang numpuk minimap/notifikasi.
    if (std::fabs(OverlayPosX - 0.02f) < 0.001f &&
        std::fabs(OverlayPosY - 0.60f) < 0.001f) {
        OverlayPosY = 0.22f;
    }

    // Auto-create if the file doesn't exist yet
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES ||
        drivetrainSchema < 6) {
        SaveConfig(module);
        WriteInt("Internal", "DrivetrainSchema", 6, ini);
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

    WriteFloat("Steering", "Attack",      SteerAttack,      ini);
    WriteFloat("Steering", "Release",     SteerRelease,     ini);
    WriteFloat("Steering", "Expo",        SteerExpo,        ini);
    WriteFloat("Steering", "DeadzonePct", SteerDeadzonePct, ini);

    WriteInt("Overlay", "Bars", OverlayBars ? 1 : 0, ini);
    WriteFloat("Overlay", "PosX",      OverlayPosX,      ini);
    WriteFloat("Overlay", "PosY",      OverlayPosY,      ini);
    WriteFloat("Overlay", "BarWidth",  OverlayBarWidth,  ini);
    WriteFloat("Overlay", "BarHeight", OverlayBarHeight, ini);
}

} // namespace Config
