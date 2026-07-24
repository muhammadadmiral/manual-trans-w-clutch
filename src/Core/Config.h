#pragma once
#include <string>
#include <vector>
#include <windows.h>

namespace Config {
extern int KeyShiftUp;
extern int KeyShiftDown;
extern int KeyClutch;
extern int KeyEngine;
extern int KeyMenu;
extern bool DebugOverlay;

// Set false to disable manual shifting on quadbikes even if they report
// more than one drive gear.
extern bool AllowQuadbikes;

// Require turning on the engine (via KeyEngine) when entering a vehicle.
extern bool RequireColdStart;

// Smoothing rates for keyboard emulation (0.01 to 1.0)
// Higher = faster response, Lower = smoother/slower response
extern float ThrottleAttack;
extern float ThrottleRelease;
extern float BrakeAttack;
extern float BrakeRelease;
extern float ClutchAttack;
extern float ClutchRelease;

// Save float setting to INI
void WriteFloat(const char *section, const char *key, float value, const char *iniPath);
void SaveConfig(HMODULE module);

// Extra VEHICLE::GET_VEHICLE_CLASS() ids to treat as automatic-only, on
// top of the hard-coded plane/heli/boat/jetski/train/bicycle checks in
// IsValidVehicle(). Empty by default (no extra exclusions).
// ini value example: "14,15,16,21" (Boats, Helicopters, Planes, Trains).
extern std::vector<int> ExcludedVehicleClasses;

// --- On-screen bar overlay (RPM / clutch / throttle / brake) ---
// This is separate from DebugOverlay (the text line) so you can run
// either, both, or neither.
extern bool OverlayBars;
extern float OverlayPosX;
extern float OverlayPosY;
extern float OverlayBarWidth;
extern float OverlayBarHeight;

void ReadConfig(HMODULE module);
} // namespace Config