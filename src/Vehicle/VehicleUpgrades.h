#pragma once

using Vehicle = int;

namespace VehicleUpgrades {

struct State {
  int engineLevel = 0;
  int engineMaxLevel = 0;
  int transmissionLevel = 0;
  int transmissionMaxLevel = 0;
  float engineStage = 0.0f;
  float transmissionStage = 0.0f;
  float stallResistance = 1.0f;
  float durabilityMultiplier = 1.0f;
  float shiftPenaltyMultiplier = 1.0f;
  bool turbo = false;
  bool motorcycle = false;
  bool raceTransmission = false;
  bool quickshifter = false;
  bool powershifter = false;
};

void Reset();
void Initialize(Vehicle vehicle);
void Refresh(Vehicle vehicle);
const State &GetState();

} // namespace VehicleUpgrades
