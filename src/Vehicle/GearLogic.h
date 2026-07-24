#pragma once
#include "../../sdk/inc/main.h"
#include "../../sdk/inc/types.h"

class VehicleData; // Forward declaration

namespace GearLogic {

// Call this when entering a new vehicle to reset logic
void Reset(int defaultGear = 0);

// Call this every frame to process shifting logic, engine stalls, and sound effects
// Returns the current manual gear
int Update(Vehicle vehicle, VehicleData& data, int maxGear, bool isUp, bool isDown, float clutch, float throttle, float speedKmH, bool& isEngineOn, int& grindWarningTimer);

// Synchronizes the calculated manual gear to the game's memory safely to prevent crashes
void ApplyToMemory(Vehicle vehicle, VehicleData& data, int manualGear, float clutch);

} // namespace GearLogic
