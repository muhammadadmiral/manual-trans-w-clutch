#pragma once

class VehicleData;
using Vehicle = int;

namespace VehicleDynamics {

struct State {
  Vehicle vehicle = 0;
  float estimatedMassKg = 1500.0f;
  float wheelbaseM = 2.6f;
  float longitudinalAcceleration = 0.0f;
  float drivelineTwist = 0.0f;
  float twistVelocity = 0.0f;
  float suspensionPitchMoment = 0.0f;
  float torqueTransfer = 1.0f;
  float previousSpeed = 0.0f;
  float previousClutchEngagement = 0.0f;
  bool initialized = false;
};

void Reset();
void SelectVehicle(Vehicle vehicle);
void Update(Vehicle vehicle, VehicleData &data, int gear, float throttle,
            float brake, float clutchEngagement, float forwardSpeed);
float GetTorqueTransfer();
const State &GetState();

} // namespace VehicleDynamics
