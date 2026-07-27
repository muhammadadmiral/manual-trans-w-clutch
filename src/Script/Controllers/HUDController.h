// =============================================================================
// HUDController.h — All HUD rendering (Gear, Speedometer, Overlays, Debug).
// =============================================================================
#pragma once

#include "../../Vehicle/VehicleData.h"
#include "../../Vehicle/VehicleProfile.h"

using Vehicle = int;

class EngineController;

namespace HUDController {

// Per-frame HUD render pass. Call after all physics calculations.
void Update(Vehicle veh, VehicleData& data,
            VehicleProfile::Drivetrain profile,
            int manualGear, int maxGear, int transmissionMode,
            float simulatedClutch, float throttle, float brake,
            float speedKmH, float rpm,
            bool isEngineOn, bool engineStarting,
            bool automaticMode, int activeSignal,
            int grindWarningTimer);

} // namespace HUDController
