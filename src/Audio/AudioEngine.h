#pragma once

#include <Windows.h>

using Vehicle = int;

namespace AudioEngine {

bool Initialize(HMODULE module);
void Shutdown();
void Update();
bool IsReady();

bool PlayManualShift(Vehicle vehicle, bool upshift, bool powerShift,
                     bool softShift);
bool PlayGearGrind(Vehicle vehicle);
bool PlayParkingBrake(bool engaged);
bool PlayAutomaticShift(Vehicle vehicle, bool selectorMove);

} // namespace AudioEngine
