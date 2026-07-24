#pragma once

#include "../../sdk/inc/natives.h"
#include "VehicleData.h"

namespace LightsLogic {

// Updates the vehicle's brake and reverse lights natively, and blocks
// the game's automatic reverse lights when braking in neutral/forward gears.
void Update(Vehicle vehicle, VehicleData &data, int manualGear,
            float brakeInput, float throttleInput);

} // namespace LightsLogic
