// =============================================================================
// TelemetryLogger.h
// Logs dynamic vehicle data to a CSV file every frame for post-session analysis.
// =============================================================================
#pragma once
#include <Windows.h>
#include <string>

using Vehicle = int;
class VehicleData;

namespace TelemetryLogger {

void StartSession(const std::string &sessionName);
void StopSession();

void LogFrame(Vehicle vehicle, VehicleData &data, float speedKmH, float rpm,
              float throttle, float brake, float clutch, int gear,
              float steeringAngle, float tcsActive, float absActive,
              float boostPressure, float oilTemp);

bool IsLogging();

} // namespace TelemetryLogger
