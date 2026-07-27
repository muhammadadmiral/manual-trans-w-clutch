// =============================================================================
// DiagnosticsController.h — Structured per-second status logging and fault
// registry. Extracted from MainScript.cpp baris 884–972.
// =============================================================================
#pragma once

#include "../../Vehicle/VehicleData.h"
#include "../../Vehicle/VehicleProfile.h"
#include <string>
#include <vector>
#include <Windows.h>

using Vehicle = int;

class DriveAssistController;

namespace DiagnosticsController {

// Register fault/event subscriptions once after DrivingEventBus::Reset().
void Initialize();

// Per-second status dump. Call every frame; internally throttles to 1x/sec.
void Update(Vehicle veh, VehicleData& data,
            VehicleProfile::Drivetrain profile,
            int manualGear, int transmissionMode,
            float driveThrottle, float absBrake, float simulatedClutch,
            float speedKmH, float forwardSpeed,
            bool isEngineOn, bool engineStarting, bool automaticMode,
            const DriveAssistController& assist);

// Record a named fault event (e.g., "money_shift", "clutch_overheat").
void RecordFault(const char* faultCode);

// Query accumulated faults for this session.
const std::vector<std::string>& GetFaults();

// Clear all faults and reset timer.
void Reset();

} // namespace DiagnosticsController
