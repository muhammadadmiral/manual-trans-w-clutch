#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

using Vehicle = int;

namespace AudioEngine {

enum class ShiftCharacter {
  Slow,
  Normal,
  Harsh
};

bool Initialize(HMODULE module);
void Shutdown();
void Update();
bool IsReady();

bool PlayShift(Vehicle vehicle, bool upshift, ShiftCharacter character,
               bool quickshifter = false);
bool PlayGearGrind(Vehicle vehicle);
bool PlayParkingBrake(bool engaged);
bool PlaySelector(Vehicle vehicle);

} // namespace AudioEngine
