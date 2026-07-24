#pragma once
#include <string>
#include <vector>
#include <windows.h>

namespace Config {
extern int KeyShiftUp;
extern int KeyShiftDown;
extern int KeyClutch;
extern bool DebugOverlay;

// Set false to disable manual shifting on quadbikes even if they report
// more than one drive gear.
extern bool AllowQuadbikes;

// Prefer writing the real Clutch offset (smoother, revs/wheelspin behave
// like an actual disengaged clutch). Falls back automatically to the
// SET_VEHICLE_CHEAT_POWER_INCREASE trick if the offset isn't writable.
// Set false to always use the cheat-power trick.
extern bool UseRealClutch;

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