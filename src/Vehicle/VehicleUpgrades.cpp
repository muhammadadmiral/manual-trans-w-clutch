#include "VehicleUpgrades.h"
#include "Maintenance/WorkshopTuning.h"
#include "../../sdk/inc/natives.h"
#include <algorithm>
#include <Windows.h>

namespace VehicleUpgrades {

static State s_state;
static Vehicle s_vehicle = 0;
static ULONGLONG s_lastRefresh = 0;

static float ResolveStage(Vehicle vehicle, int modType, int &level,
                          int &maxLevel) {
  const int installedIndex = VEHICLE::GET_VEHICLE_MOD(vehicle, modType);
  maxLevel =
      (std::max)(0, VEHICLE::GET_NUM_VEHICLE_MODS(vehicle, modType));
  level = installedIndex >= 0 ? installedIndex + 1 : 0;
  return maxLevel > 0
             ? std::clamp(static_cast<float>(level) /
                              static_cast<float>(maxLevel),
                          0.0f, 1.0f)
             : 0.0f;
}

void Reset() {
  s_state = State{};
  s_vehicle = 0;
  s_lastRefresh = 0;
}

void Initialize(Vehicle vehicle) {
  s_state = State{};
  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
  s_state.motorcycle =
      VEHICLE::IS_THIS_MODEL_A_BIKE(model) ||
      VEHICLE::IS_THIS_MODEL_A_QUADBIKE(model);
  s_state.engineStage =
      ResolveStage(vehicle, 11, s_state.engineLevel, s_state.engineMaxLevel);
  s_state.transmissionStage =
      ResolveStage(vehicle, 13, s_state.transmissionLevel,
                   s_state.transmissionMaxLevel);
  s_state.transmissionStage =
      std::max(s_state.transmissionStage,
               WorkshopTuning::GetTransmissionStage());
  s_state.turbo = VEHICLE::IS_TOGGLE_MOD_ON(vehicle, 18) != FALSE;
  const bool nativeRaceTransmission =
      s_state.transmissionMaxLevel > 0 &&
      s_state.transmissionLevel >= s_state.transmissionMaxLevel;
  s_state.raceTransmission =
      nativeRaceTransmission || WorkshopTuning::IsRaceTransmission();

  s_state.stallResistance =
      1.0f + s_state.engineStage * 0.45f +
      s_state.transmissionStage * 0.10f;
  s_state.durabilityMultiplier =
      s_state.raceTransmission
          ? 0.0000001f
          : 1.0f / (1.0f + s_state.engineStage * 0.40f +
                    s_state.transmissionStage * 0.30f);
  s_state.shiftPenaltyMultiplier =
      s_state.raceTransmission
          ? 0.0000001f
          : 1.0f - s_state.transmissionStage * 0.62f;

  // Racing transmission unlocks both strategies for both platforms. The
  // active shift path still decides which one is mechanically appropriate:
  // clutch-open no-lift uses powershift, clutchless upshift uses quickshift.
  s_state.quickshifter =
      s_state.raceTransmission ||
      (s_state.motorcycle && s_state.transmissionStage >= 0.34f);
  s_state.powershifter = s_state.raceTransmission;
  s_vehicle = vehicle;
  s_lastRefresh = GetTickCount64();
}

void Refresh(Vehicle vehicle) {
  const ULONGLONG now = GetTickCount64();
  if (vehicle != s_vehicle || now - s_lastRefresh >= 1000)
    Initialize(vehicle);
}

const State &GetState() {
  return s_state;
}

} // namespace VehicleUpgrades
