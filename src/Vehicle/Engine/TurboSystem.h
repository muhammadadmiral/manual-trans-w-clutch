// =============================================================================
// TurboSystem
// Simulates turbo lag and boost curves. Modulates the vehicle's engine power
// dynamically based on simulated exhaust gas pressure.
// =============================================================================
#pragma once
#include <Windows.h>

using Vehicle = int;
class VehicleData;

namespace TurboSystem {

struct TurboState {
  bool hasTurbo = false;
  bool blowOffLatched = false;
  bool flutterLatched = false;
  bool nativeBoostActive = false;
  Vehicle vehicle = 0;
  float spool = 0.0f;
  float boostPressure = 0.0f;
  float previousThrottle = 0.0f;
};

void Reset();

// Check if the current vehicle has the turbo upgrade applied in-game
void InitializeForVehicle(Vehicle vehicle);

// Updates spool logic and returns the power multiplier to apply to the engine
float Update(Vehicle vehicle, VehicleData &data, float rpm, float throttle, bool isEngineOn);

float GetBoostPressure();
bool HasTurbo();

} // namespace TurboSystem
