// =============================================================================
// TurboSystem.h
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
  float spool = 0.0f;       // 0.0 to 1.0 (max boost)
  float boostPressure = 0.0f; // Actual pressure delivered
};

void Reset();

// Check if the current vehicle has the turbo upgrade applied in-game
void InitializeForVehicle(Vehicle vehicle);

// Updates spool logic and returns the power multiplier to apply to the engine
float Update(Vehicle vehicle, VehicleData &data, float rpm, float throttle, bool isEngineOn);

float GetBoostPressure();
bool HasTurbo();

} // namespace TurboSystem
