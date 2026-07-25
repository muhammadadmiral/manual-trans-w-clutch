// =============================================================================
// FuelSystem.cpp  —  Realistic fuel consumption, oil temp, refueling
// =============================================================================
#include "FuelSystem.h"
#include "VehicleData.h"
#include "../../sdk/inc/natives.h"
#include <cmath>
#include <cstdlib>
#include <Windows.h>

namespace FuelSystem {

static FuelState s_state;

// ─── Helpers ─────────────────────────────────────────────────────────────────
static inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float Lerp(float a, float b, float t) { return a + (b - a) * Clamp01(t); }

// Per-frame consumption based on load (throttle + RPM)
static float ComputeConsumptionRate(float rpm, float throttle, bool isEngineOn) {
  if (!isEngineOn) return 0.0f;
  float load   = throttle * 0.6f + rpm * 0.4f;
  return Lerp(kIdleConsumptionRate, kFullThrottleConsumptionRate, load);
}

// ─── API ─────────────────────────────────────────────────────────────────────

void Reset(float savedFuelLevel) {
  s_state              = FuelState{};
  s_state.fuelLevel    = Clamp01(savedFuelLevel);
  s_state.oilTemperature = kColdOilTemp;
}

bool Update(Vehicle vehicle, VehicleData &data, float throttle, float rpm,
            bool isEngineOn, float speedKmH) {

  // Sync with game's fuel if it changed externally (cheats, etc)
  float memFuel = data.GetFuelLevel();
  if (memFuel > 0.0f && memFuel <= 1.0f) {
    if ((s_state.fuelLevel - memFuel > 0.05f) ||
        (memFuel - s_state.fuelLevel > 0.05f)) {
      s_state.fuelLevel = memFuel;
    }
  }

  // ── Refueling ─────────────────────────────────────────────────────────────
  if (!isEngineOn && speedKmH < 1.0f && s_state.fuelLevel < 1.0f) {
    s_state.isRefueling = true;
  }

  if (s_state.isRefueling) {
    s_state.fuelLevel += kRefuelRate;
    if (s_state.fuelLevel >= 1.0f) {
      s_state.fuelLevel            = 1.0f;
      s_state.isRefueling          = false;
      s_state.lowFuelNotified      = false;
      s_state.criticalFuelNotified = false;
      s_state.distanceSinceRefuel  = 0.0f;
    }
  }

  // ── Consumption ───────────────────────────────────────────────────────────
  float consumption    = ComputeConsumptionRate(rpm, throttle, isEngineOn);
  float speedMult      = 1.0f + speedKmH / 200.0f;
  consumption         *= speedMult;

  s_state.fuelLevel   -= consumption;
  s_state.fuelLevel    = Clamp01(s_state.fuelLevel);
  s_state.fuelConsumedTotal    += consumption;
  s_state.distanceSinceRefuel  += speedKmH / 216000.0f; // km per frame at 60fps

  // ── Oil Temperature ───────────────────────────────────────────────────────
  if (isEngineOn) {
    float heatRate = kOilWarmRate * (1.0f + rpm * 0.5f + throttle * 0.5f);
    s_state.oilTemperature += heatRate;
  } else {
    s_state.oilTemperature -= kOilCoolRate;
  }
  s_state.oilTemperature = Clamp01(s_state.oilTemperature);

  // Overheating visual effect: slight RPM flicker
  if (s_state.oilTemperature > kHotOilTemp && isEngineOn) {
    float currentRPM = data.GetRPM();
    float flicker    = std::sin(static_cast<float>(GetTickCount()) * 0.05f) * 0.02f;
    float newRPM     = currentRPM + flicker;
    newRPM = newRPM < 0.0f ? 0.0f : (newRPM > 1.0f ? 1.0f : newRPM);
    data.SetRPM(newRPM);
  }

  // ── Low Fuel Warnings ─────────────────────────────────────────────────────
  if (!s_state.lowFuelNotified && s_state.fuelLevel < kLowFuelThreshold) {
    s_state.lowFuelNotified = true;
    HUD::BEGIN_TEXT_COMMAND_THEFEED_POST("STRING");
    HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME("~y~LOW FUEL! Find a gas station.");
    HUD::END_TEXT_COMMAND_THEFEED_POST_TICKER(false, false);
  }
  if (!s_state.criticalFuelNotified && s_state.fuelLevel < kCriticalFuelThreshold) {
    s_state.criticalFuelNotified = true;
    HUD::BEGIN_TEXT_COMMAND_THEFEED_POST("STRING");
    HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME("~r~CRITICAL FUEL! Engine may die!");
    HUD::END_TEXT_COMMAND_THEFEED_POST_TICKER(false, false);
  }

  // ── Engine Kill on Empty ──────────────────────────────────────────────────
  if (s_state.fuelLevel <= kEngineKillThreshold && isEngineOn) {
    return true;
  }

  return false;
}

void StartRefuel() { if (s_state.fuelLevel < 1.0f) s_state.isRefueling = true; }
void StopRefuel()  { s_state.isRefueling = false; }

float GetFuelLevel()          { return s_state.fuelLevel;      }
float GetOilTemperature()     { return s_state.oilTemperature; }
bool  IsLowFuel()             { return s_state.fuelLevel < kLowFuelThreshold;     }
bool  IsCriticalFuel()        { return s_state.fuelLevel < kCriticalFuelThreshold; }
bool  IsRefueling()           { return s_state.isRefueling;    }
const FuelState &GetState()   { return s_state;                }

void SaveToIni(const char *iniPath) {
  char buf[32]{};
  sprintf_s(buf, "%.4f", s_state.fuelLevel);
  WritePrivateProfileStringA("Simulation", "FuelLevel", buf, iniPath);
  sprintf_s(buf, "%.4f", s_state.oilTemperature);
  WritePrivateProfileStringA("Simulation", "OilTemperature", buf, iniPath);
  sprintf_s(buf, "%.4f", s_state.distanceSinceRefuel);
  WritePrivateProfileStringA("Simulation", "DistanceSinceRefuel", buf, iniPath);
}

void LoadFromIni(const char *iniPath) {
  char buf[32]{};
  GetPrivateProfileStringA("Simulation", "FuelLevel", "1.0", buf, sizeof(buf), iniPath);
  s_state.fuelLevel = Clamp01(static_cast<float>(std::atof(buf)));
  GetPrivateProfileStringA("Simulation", "OilTemperature", "0.0", buf, sizeof(buf), iniPath);
  s_state.oilTemperature = Clamp01(static_cast<float>(std::atof(buf)));
  GetPrivateProfileStringA("Simulation", "DistanceSinceRefuel", "0.0", buf, sizeof(buf), iniPath);
  float d = static_cast<float>(std::atof(buf));
  s_state.distanceSinceRefuel = d < 0.0f ? 0.0f : d;
}

} // namespace FuelSystem
