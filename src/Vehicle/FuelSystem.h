// =============================================================================
// FuelSystem.h
// Simulates realistic fuel consumption, oil temperature, fuel gauge HUD,
// and refueling mechanics. Reads and writes FuelLevel/OilLevel from memory.
// =============================================================================
#pragma once
#include <cstdint>
#include <Windows.h>

class VehicleData;
using Vehicle = int;

namespace FuelSystem {

// ─── Fuel Configuration ───────────────────────────────────────────────────────
// Consumption rates in fuel-fraction per second (0.0 to 1.0 is full tank).
// At full throttle a typical car does ~12L/100km; translate to game units.
constexpr float kIdleConsumptionRate     = 0.0000010f; // per frame at idle RPM
constexpr float kCruiseConsumptionRate   = 0.0000035f; // per frame at cruise
constexpr float kFullThrottleConsumptionRate = 0.0000095f; // WOT per frame
constexpr float kLowFuelThreshold        = 0.15f;     // 15% = low fuel warning
constexpr float kCriticalFuelThreshold   = 0.05f;     // 5% = critical
constexpr float kRefuelRate              = 0.001f;    // per frame when refueling
constexpr float kEngineKillThreshold     = 0.005f;    // engine dies below this

// ─── Oil Temperature ──────────────────────────────────────────────────────────
constexpr float kColdOilTemp             = 0.0f;   // 20°C normalized
constexpr float kWarmOilTemp             = 0.5f;   // 80°C optimal
constexpr float kHotOilTemp              = 0.85f;  // 120°C overheating
constexpr float kCriticalOilTemp         = 1.0f;   // 140°C+
constexpr float kOilWarmRate             = 0.00004f;
constexpr float kOilCoolRate             = 0.00002f;
constexpr float kOverheatDamageRate      = 0.00005f; // hp damage when too hot

struct FuelState {
  float fuelLevel = 1.0f;        // 0.0 (empty) to 1.0 (full)
  float oilTemperature = 0.0f;   // 0.0 (cold) to 1.0 (overheating)
  float engineLoad = 0.0f;       // smoothed throttle load for consumption calc
  bool  lowFuelNotified = false;
  bool  criticalFuelNotified = false;
  bool  isRefueling = false;
  int   refuelCooldown = 0;

  // Consumption tracking
  float distanceSinceRefuel = 0.0f; // cumulative km since last full tank
  float fuelConsumedTotal = 0.0f;   // total fuel consumed this session
};

// ─── API ──────────────────────────────────────────────────────────────────────

// Reset fuel state (use saved fuel level or default full tank).
void Reset(float savedFuelLevel = 1.0f);

// Called every frame when player is in a valid vehicle with engine running.
// Returns true if engine ran out of fuel this frame (trigger stall).
bool Update(Vehicle vehicle, VehicleData& data, float throttle, float rpm,
            bool isEngineOn, float speedKmH);

// Trigger a refuel event (call when player is near a gas station).
void StartRefuel();
void StopRefuel();

// Query current state for HUD / saving
float GetFuelLevel();
float GetOilTemperature();
bool  IsLowFuel();
bool  IsCriticalFuel();
bool  IsRefueling();
const FuelState& GetState();

// Save/load fuel level to INI so it persists between sessions
void SaveToIni(const char* iniPath);
void LoadFromIni(const char* iniPath);

} // namespace FuelSystem
