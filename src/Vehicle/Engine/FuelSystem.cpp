// Fuel dan temperatur. Semua rate wajib dikali delta-time.
#include "FuelSystem.h"
#include "../VehicleData.h"
#include "../../Core/Config.h"
#include "../../../sdk/inc/natives.h"
#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <Windows.h>

namespace FuelSystem {

static FuelState s_state;
static std::unordered_map<std::uint64_t, FuelState> s_vehicleStates;
static std::uint64_t s_activeVehicleKey = 0;

static std::uint64_t VehicleKey(Vehicle vehicle) {
  const std::uint64_t model =
      static_cast<std::uint32_t>(ENTITY::GET_ENTITY_MODEL(vehicle));
  const char *plate = VEHICLE::GET_VEHICLE_NUMBER_PLATE_TEXT(vehicle);
  std::uint64_t hash = 1469598103934665603ull;
  if (plate) {
    for (const unsigned char *p =
             reinterpret_cast<const unsigned char *>(plate);
         *p; ++p) {
      hash ^= *p;
      hash *= 1099511628211ull;
    }
  }
  return (model << 32) ^ hash;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────
static inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float Lerp(float a, float b, float t) { return a + (b - a) * Clamp01(t); }

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

void SelectVehicle(Vehicle vehicle) {
  if (!vehicle || !ENTITY::DOES_ENTITY_EXIST(vehicle))
    return;
  if (s_activeVehicleKey)
    s_vehicleStates[s_activeVehicleKey] = s_state;
  s_activeVehicleKey = VehicleKey(vehicle);
  const auto it = s_vehicleStates.find(s_activeVehicleKey);
  if (it != s_vehicleStates.end()) {
    s_state = it->second;
  } else {
    Reset(1.0f);
    s_vehicleStates.emplace(s_activeVehicleKey, s_state);
  }
}

bool Update(Vehicle vehicle, VehicleData &data, float throttle, float rpm,
            bool isEngineOn, float speedKmH, int gear,
            float clutchEngagement) {
  if (!Config::FuelEnabled)
    return false;
  const float dt = std::fminf(MISC::GET_FRAME_TIME(), 0.05f);
  const float frameScale = dt * 60.0f;

  if (s_state.isRefueling) {
    s_state.fuelLevel += kRefuelRate * frameScale;
    if (s_state.fuelLevel >= 1.0f) {
      s_state.fuelLevel            = 1.0f;
      s_state.isRefueling          = false;
      s_state.lowFuelNotified      = false;
      s_state.criticalFuelNotified = false;
      s_state.distanceSinceRefuel  = 0.0f;
    }
  }

  // ── Consumption ───────────────────────────────────────────────────────────
  s_state.decelerationFuelCut =
      Config::FuelCutoffEngineBrake && isEngineOn && gear != 0 &&
      clutchEngagement > 0.80f && throttle < 0.01f &&
      rpm > 0.30f && speedKmH > 8.0f;
  float consumption =
      s_state.decelerationFuelCut
          ? 0.0f
          : ComputeConsumptionRate(rpm, throttle, isEngineOn);
  float speedMult      = 1.0f + speedKmH / 200.0f;
  consumption         *= speedMult * frameScale;

  s_state.fuelLevel   -= consumption;
  s_state.fuelLevel    = Clamp01(s_state.fuelLevel);
  s_state.fuelConsumedTotal    += consumption;
  s_state.distanceSinceRefuel += speedKmH * dt / 3600.0f;

  // ── Oil Temperature ───────────────────────────────────────────────────────
  if (isEngineOn) {
    float heatRate = kOilWarmRate * (1.0f + rpm * 0.5f + throttle * 0.5f);
    s_state.oilTemperature += heatRate * frameScale;
  } else {
    s_state.oilTemperature -= kOilCoolRate * frameScale;
  }
  s_state.oilTemperature = Clamp01(s_state.oilTemperature);

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

  (void)vehicle;
  (void)data;
  return false;
}

void StartRefuel() { if (s_state.fuelLevel < 1.0f) s_state.isRefueling = true; }
void StopRefuel()  { s_state.isRefueling = false; }
void AddFuel(float normalizedAmount) {
  if (normalizedAmount <= 0.0f)
    return;
  s_state.fuelLevel = Clamp01(s_state.fuelLevel + normalizedAmount);
  if (s_state.fuelLevel >= 1.0f) {
    s_state.lowFuelNotified = false;
    s_state.criticalFuelNotified = false;
    s_state.distanceSinceRefuel = 0.0f;
  }
  if (s_activeVehicleKey)
    s_vehicleStates[s_activeVehicleKey] = s_state;
}

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
