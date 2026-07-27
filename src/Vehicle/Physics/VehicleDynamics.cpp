#include "VehicleDynamics.h"

#include "../VehicleData.h"
#include "../../../sdk/inc/natives.h"

#include <algorithm>
#include <cmath>

namespace VehicleDynamics {
namespace {

State s_state;

} // namespace

void Reset() {
  s_state = State{};
}

void SelectVehicle(Vehicle vehicle) {
  if (vehicle == s_state.vehicle)
    return;
  Reset();
  s_state.vehicle = vehicle;

  // Dimensions are telemetry only. Do not guess mass from vehicle class and
  // never use model size to synthesize drivetrain torque.
  Vector3 minimum{};
  Vector3 maximum{};
  MISC::GET_MODEL_DIMENSIONS(ENTITY::GET_ENTITY_MODEL(vehicle),
                             &minimum, &maximum);
  const float length = std::max(0.0f, maximum.y - minimum.y);
  s_state.wheelbaseM = length > 0.0f ? length * 0.62f : 0.0f;
  s_state.estimatedMassKg = 0.0f;
  s_state.torqueTransfer = 1.0f;
}

void Update(Vehicle vehicle, VehicleData &data, int gear, float throttle,
            float brake, float clutchEngagement, float forwardSpeed) {
  if (vehicle != s_state.vehicle)
    SelectVehicle(vehicle);

  const float dt = std::clamp(MISC::GET_FRAME_TIME(), 0.001f, 0.05f);
  if (!s_state.initialized) {
    s_state.previousSpeed = forwardSpeed;
    s_state.previousClutchEngagement = clutchEngagement;
    s_state.initialized = true;
  }

  const float acceleration =
      (forwardSpeed - s_state.previousSpeed) / dt;
  const float alpha = 1.0f - std::exp(-6.0f * dt);
  s_state.longitudinalAcceleration +=
      (acceleration - s_state.longitudinalAcceleration) * alpha;
  s_state.previousSpeed = forwardSpeed;
  s_state.previousClutchEngagement = clutchEngagement;

  // Observer-only: handling/CVehicle own torque transfer and suspension.
  s_state.drivelineTwist = 0.0f;
  s_state.twistVelocity = 0.0f;
  s_state.suspensionPitchMoment = 0.0f;
  s_state.torqueTransfer = 1.0f;

  (void)data;
  (void)gear;
  (void)throttle;
  (void)brake;
}

float GetTorqueTransfer() {
  return 1.0f;
}

const State &GetState() {
  return s_state;
}

} // namespace VehicleDynamics
