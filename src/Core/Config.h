#pragma once
#include <string>
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

void ReadConfig(HMODULE module);
} // namespace Config