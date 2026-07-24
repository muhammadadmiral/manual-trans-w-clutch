// =============================================================================
// TelemetryLogger.cpp
// =============================================================================
#include "TelemetryLogger.h"
#include <fstream>
#include <iomanip>

namespace TelemetryLogger {

static std::ofstream s_file;
static bool s_isLogging = false;
static uint64_t s_startTime = 0;

void StartSession(const std::string &sessionName) {
  if (s_isLogging) return;
  
  char path[MAX_PATH];
  GetModuleFileNameA(nullptr, path, MAX_PATH);
  std::string dir = path;
  size_t pos = dir.find_last_of("\\/");
  if (pos != std::string::npos) {
    dir = dir.substr(0, pos + 1);
  }
  
  std::string fullPath = dir + "telemetry_" + sessionName + ".csv";
  s_file.open(fullPath, std::ios::out | std::ios::trunc);
  
  if (s_file.is_open()) {
    s_isLogging = true;
    s_startTime = GetTickCount64();
    s_file << "Time(ms),Speed(km/h),RPM,Gear,Throttle,Brake,Clutch,Steering,TCS,ABS,Boost,OilTemp\n";
  }
}

void StopSession() {
  if (s_isLogging) {
    s_file.close();
    s_isLogging = false;
  }
}

void LogFrame(Vehicle vehicle, VehicleData &data, float speedKmH, float rpm,
              float throttle, float brake, float clutch, int gear,
              float steeringAngle, float tcsActive, float absActive,
              float boostPressure, float oilTemp) {
  if (!s_isLogging || !s_file.is_open()) return;
  
  uint64_t elapsed = GetTickCount64() - s_startTime;
  
  s_file << std::fixed << std::setprecision(4)
         << elapsed << ","
         << speedKmH << ","
         << rpm << ","
         << gear << ","
         << throttle << ","
         << brake << ","
         << clutch << ","
         << steeringAngle << ","
         << tcsActive << ","
         << absActive << ","
         << boostPressure << ","
         << oilTemp << "\n";
}

bool IsLogging() {
  return s_isLogging;
}

} // namespace TelemetryLogger
