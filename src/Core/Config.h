// Konfigurasi runtime. Detail tiap parameter ada di docs/configuration.md.
#pragma once
#include <string>
#include <vector>
#include <Windows.h>

namespace Config {

// 0 = drivetrain mod off, 1 = automatic P-R-N-D-S, 2 = manual sequential.
extern int TransmissionMode;

// ── Digital keys (virtual-key codes) ─────────────────────────────────────────
extern int KeyShiftUp;
extern int KeyShiftDown;
extern int KeyClutch;
extern int KeyEngine;
extern int KeyMenu;

// Turn signal keys. Tapping left/right toggles; tapping opposite side switches.
// KeySignalHazard activates BOTH signals simultaneously (hazard lights).
extern int KeySignalLeft;
extern int KeySignalRight;
extern int KeySignalHazard;    // H by default (0x48)
extern int KeyParkingBrake;    // P by default
extern int KeyRefuel;
extern int KeyOilService;

// Automatically cancel the active turn signal when the steering wheel returns
// through centre (raw steer crosses 0 in the opposite direction of the signal).
extern bool SignalAutoCancelSteer;

// ── Feature flags ─────────────────────────────────────────────────────────────
extern bool DebugOverlay;
extern bool AllowQuadbikes;
extern bool UseRealClutch;
extern bool RequireColdStart;
extern bool ForceRecalibrate;
extern bool LaunchControl;
extern float LaunchControlRPM;

extern bool TcsEnabled;
extern float TcsSlipTarget;
extern float TcsMaxCut;
extern bool AbsEnabled;
extern float AbsSlipTarget;
extern float AbsMaxRelease;

extern bool IdleCreep;
extern float IdleCreepThrottle;
extern bool StallEnabled;
extern float StallRate;
extern float StallClutchThreshold;
extern float IdleTorqueFraction;
extern float LugStallRPM;
extern float LugStallDelay;
extern float WaterStallDelay;
extern float RolloverStallDelay;
extern float RevHangDuration;
extern bool HardBrakeStall;
extern bool StarterInterlock;
extern bool AutomaticStartRequiresBrake;

extern float ClutchBiteStart;
extern float ClutchBiteEnd;
extern float ClutchHeatRate;
extern float ClutchCoolRate;
extern float ClutchFadeStart;
extern float ClutchFadeStrength;
extern float MaxClutchTorque;
extern bool ClutchJudder;

extern bool GearClash;
extern float GearGrindDamage;
extern float ShiftShockStrength;
extern float NoLiftShiftPenalty;
extern bool SynchronizerWear;
extern bool ShiftResistance;
extern bool NativeGearboxPatch;
extern bool FuelCutoffEngineBrake;
extern float ConnectedRPMSync;
extern bool AutomaticBrakeInterlock;
extern float AutomaticShiftDelay;
extern float AutomaticDUpRPM;
extern float AutomaticDDownRPM;
extern float AutomaticSUpRPM;
extern float AutomaticSDownRPM;
extern float AutomaticKickdownThrottle;
extern float AutomaticSTorqueBoost;
extern float AutomaticDKeyboardThrottle;
extern float AutomaticKickdownDelay;
extern bool AutomaticTCC;
extern bool AutomaticFluidOverheat;
extern bool AutomaticNeutralDropDamage;
extern bool AutomaticBrakeBoostStall;
extern float AutomaticThrottleAttack;
extern float AutomaticThrottleRelease;
extern float AutomaticBrakeAttack;
extern float AutomaticBrakeRelease;
extern bool BrakeThrottleOverride;
extern float BrakeOverrideDelay;
extern float BrakeOverrideCut;
extern float ReverseLockoutSpeedKmH;
extern float OverRevShiftDamage;
extern float ClutchDumpRate;
extern float ClutchDumpShock;
extern bool BrakeFadeEnabled;
extern float BrakeHeatRate;
extern float BrakeCoolRate;
extern float BrakeFadeStart;
extern float BrakeFadeStrength;

// Audio mekanikal. Suara mesin, ban, turbo dan angin tetap punya GTA.
extern bool AudioEnabled;
extern float AudioMasterVolume;
extern float AudioPitchRandomness;
extern float AudioLimiterCeiling;
extern bool AudioNativeLayers;

// Fuel dan maintenance.
extern bool FuelEnabled;
extern float RefuelRatePerSecond;
extern bool MaintenanceEnabled;
extern float OilWearMultiplier;

// ── Analog smoothing — time constants τ in seconds ────────────────────────────
// See header comment above for interpretation.
// Recommended starting defaults (set in Config.cpp):
//   Throttle: attack=0.10  release=0.25
//   Brake:    attack=0.08  release=0.20
//   Clutch:   attack=0.05  release=0.07
//   Steer:    attack=0.06  release=0.12
extern float ThrottleAttack;
extern float ThrottleRelease;
extern float BrakeAttack;
extern float BrakeRelease;
extern float ClutchAttack;
extern float ClutchRelease;

// Steering
extern float SteerAttack;
extern float SteerRelease;
extern float SteerExpo;          // 0.0-1.0 (0=linear, 1=full cubic)
extern float SteerDeadzonePct;   // 0.0-0.15 recommended

// Expo curves on throttle/brake/clutch output (0=linear, 1=cubic)
extern float ThrottleExpo;
extern float BrakeExpo;
extern float ClutchExpo;

// ── Excluded vehicle classes ──────────────────────────────────────────────────
// VEHICLE::GET_VEHICLE_CLASS() ids to treat as automatic-only.
// Example ini value: "14,15,16,21" (Boats, Helicopters, Planes, Trains)
extern std::vector<int> ExcludedVehicleClasses;

// ── HUD Overlay ───────────────────────────────────────────────────────────────
extern bool  OverlayBars;
extern bool  GearHudEnabled;
extern float OverlayPosX;
extern float OverlayPosY;
extern float OverlayBarWidth;
extern float OverlayBarHeight;
extern float GearHudPosX;
extern float GearHudPosY;
extern float GearHudScale;
extern float MenuPosX;
extern float MenuPosY;
extern float MenuScale;

// ── Functions ─────────────────────────────────────────────────────────────────
void ReadConfig(HMODULE module);
void SaveConfig(HMODULE module);
void WriteFloat(const char* section, const char* key, float value, const char* iniPath);

} // namespace Config
