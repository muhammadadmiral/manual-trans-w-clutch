#pragma once

class VehicleData;
using Vehicle = int;

namespace EngineModel {

struct State {
  float idleRPM = 0.2f;
  float inertia = 1.0f;
  float load = 0.0f;
  float stallProgress = 0.0f;
  float engineBrake = 0.0f;
  float expectedRPM = 0.2f;
  float creepThrottle = 0.0f;
  float torqueReserve = 0.0f;
  float previousRPM = 0.2f;
  float controlledRPM = 0.2f;
  float wheelRPM = 0.2f;
  float estimatedFlatVelocity = 0.0f;
  bool freeRevActive = false;
  bool rpmOwned = false;
  bool handlingBacked = false;
};

void Reset();

// Return true kalau beban drivetrain sukses bikin mesin mati.
bool Update(Vehicle vehicle, VehicleData &data, int gear, int maxGear,
            float clutchDisengagement, float clutchEngagement,
            float throttle, float brake, float speedMps, bool engineOn,
            bool automaticMode = false);

float GetLoad();
float GetStallProgress();
float GetEngineBrake();
float GetInertia();
float GetExpectedRPM();
float GetCreepThrottle();
float GetTorqueReserve();
const State &GetState();

} // namespace EngineModel
