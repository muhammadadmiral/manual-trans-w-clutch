#pragma once

class VehicleData;
using Vehicle = int;

namespace ClutchSystem {

struct State {
  float disengagement = 0.0f;
  float engagement = 1.0f;
  float heat = 0.0f;
  float nativeActuator = 1.0f;
  float previousDisengagement = 0.0f;
  float actuatorDisengagement = 0.0f;
  float releaseRate = 0.0f;
  float handlingDriveForce = 0.30f;
  float handlingClutchRate = 2.0f;
  float biteRatePerSecond = 9.0f;
  float dumpSeverity = 0.0f;
  float dumpRemaining = 0.0f;
  float overloadSlip = 0.0f;
  float torqueDemand = 0.0f;
  float torqueCapacity = 1.0f;
  float packageCapacityMultiplier = 1.0f;
  float judder = 0.0f;
  float judderPhase = 0.0f;
  bool slipping = false;
  bool overloaded = false;
};

void Reset();
void ServiceClutch();

// rawPedal: 0 dilepas, 1 diinjak penuh.
float UpdatePedal(VehicleData &data, float rawPedal, float throttle,
                  float rpm, int gear, int maxGear, bool engineOn);

// Logical gear tetap kepasang; hard-open memakai carrier gear 1 di ManualGearbox.
void ApplyToVehicle(VehicleData &data, int gear, float speedMps);

float GetEngagement();
float GetHeat();
float GetNativeActuator();
float GetDumpSeverity();
bool IsDumpActive();
bool IsDrivelineOpen(int gear);
const State &GetState();

} // namespace ClutchSystem
