#include "VehicleDynamics.h"

#include "../Maintenance/WorkshopTuning.h"
#include "../VehicleData.h"
#include "../../Script/DrivingEventBus.h"
#include "../../../sdk/inc/natives.h"

#include <algorithm>
#include <cmath>

namespace VehicleDynamics {
namespace {

State s_state;
ULONGLONG s_lastClunkAt = 0;

float Clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

float EstimateClassMass(Vehicle vehicle) {
  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
  if (VEHICLE::IS_THIS_MODEL_A_BIKE(model) ||
      VEHICLE::IS_THIS_MODEL_A_QUADBIKE(model))
    return 260.0f;
  switch (VEHICLE::GET_VEHICLE_CLASS(vehicle)) {
  case 0: return 1120.0f; // compacts
  case 1: return 1450.0f; // sedans
  case 2: return 2050.0f; // SUVs
  case 3: return 1520.0f; // coupes
  case 4: return 1700.0f; // muscle
  case 5: return 1380.0f; // classics
  case 6: return 1480.0f; // sports
  case 7: return 1420.0f; // super
  case 9: return 1850.0f; // off-road
  case 10: return 6200.0f;
  case 11: return 2800.0f;
  case 12: return 2350.0f;
  case 18: return 1950.0f;
  case 20: return 8500.0f;
  default: return 1550.0f;
  }
}

} // namespace

void Reset() {
  s_state = State{};
  s_lastClunkAt = 0;
}

void SelectVehicle(Vehicle vehicle) {
  if (vehicle == s_state.vehicle)
    return;
  Reset();
  s_state.vehicle = vehicle;
  Vector3 minimum{};
  Vector3 maximum{};
  MISC::GET_MODEL_DIMENSIONS(ENTITY::GET_ENTITY_MODEL(vehicle),
                             &minimum, &maximum);
  const float length = std::max(1.0f, maximum.y - minimum.y);
  const float width = std::max(0.5f, maximum.x - minimum.x);
  const float height = std::max(0.5f, maximum.z - minimum.z);
  const float volumeScale =
      std::clamp((length * width * height) / 14.0f, 0.72f, 1.38f);
  s_state.estimatedMassKg =
      EstimateClassMass(vehicle) * std::sqrt(volumeScale);
  s_state.wheelbaseM = std::clamp(length * 0.62f, 1.35f, 4.80f);
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

  const float rawAcceleration =
      std::clamp((forwardSpeed - s_state.previousSpeed) / dt,
                 -14.0f, 14.0f);
  const float accelerationAlpha = 1.0f - std::exp(-6.0f * dt);
  s_state.longitudinalAcceleration +=
      (rawAcceleration - s_state.longitudinalAcceleration) *
      accelerationAlpha;
  s_state.previousSpeed = forwardSpeed;

  const float handlingDriveForce = data.GetInitialDriveForce();
  const float driveForce =
      std::clamp(handlingDriveForce > 0.0f
                     ? handlingDriveForce
                     : 0.30f,
                 0.05f, 1.50f);
  const float massScale =
      std::clamp(s_state.estimatedMassKg / 1500.0f, 0.18f, 5.5f);
  const float direction = gear < 0 ? -1.0f : (gear > 0 ? 1.0f : 0.0f);
  const float appliedTorque =
      direction * Clamp01(throttle) * Clamp01(clutchEngagement) *
      std::sqrt(driveForce / 0.30f);
  const float brakeTorque =
      Clamp01(brake) * (forwardSpeed >= 0.0f ? -1.0f : 1.0f) * 0.55f;
  const float mountFlex =
      WorkshopTuning::GetDrivetrainFlexMultiplier();
  const float targetTwist =
      std::clamp((appliedTorque + brakeTorque) *
                     std::sqrt(massScale) * mountFlex,
                 -1.0f, 1.0f);

  // Damped torsional spring. It stores a small amount of load on throttle
  // application and returns it progressively; it never changes RPM/redline.
  const float stiffness = 18.0f / std::max(0.18f, mountFlex);
  const float damping = 7.5f + (1.0f - mountFlex) * 6.0f;
  const float twistAcceleration =
      (targetTwist - s_state.drivelineTwist) * stiffness -
      s_state.twistVelocity * damping;
  s_state.twistVelocity += twistAcceleration * dt;
  s_state.drivelineTwist += s_state.twistVelocity * dt;
  s_state.drivelineTwist =
      std::clamp(s_state.drivelineTwist, -1.15f, 1.15f);
  const float torsionalLag =
      std::fabs(targetTwist - s_state.drivelineTwist);
  const float elasticReturn =
      s_state.twistVelocity * direction * 0.012f;
  s_state.torqueTransfer =
      std::clamp(1.0f - torsionalLag * 0.10f + elasticReturn,
                 0.88f, 1.02f);

  const float loadTransfer =
      s_state.longitudinalAcceleration / 9.80665f *
      std::clamp(std::sqrt(massScale), 0.45f, 2.2f);
  s_state.suspensionPitchMoment =
      std::clamp(loadTransfer * 0.022f * mountFlex, -0.045f, 0.045f);
  if (!ENTITY::IS_ENTITY_IN_AIR(vehicle) &&
      std::fabs(s_state.suspensionPitchMoment) > 0.002f) {
    Vector3 minimum{};
    Vector3 maximum{};
    MISC::GET_MODEL_DIMENSIONS(ENTITY::GET_ENTITY_MODEL(vehicle),
                               &minimum, &maximum);
    const float front = std::clamp(maximum.y * 0.68f, 0.45f, 2.5f);
    const float rear = std::clamp(minimum.y * 0.68f, -2.5f, -0.45f);
    ENTITY::APPLY_FORCE_TO_ENTITY(
        vehicle, 1, 0.0f, 0.0f, s_state.suspensionPitchMoment,
        0.0f, front, 0.0f, 0, TRUE, TRUE, TRUE, FALSE, TRUE);
    ENTITY::APPLY_FORCE_TO_ENTITY(
        vehicle, 1, 0.0f, 0.0f, -s_state.suspensionPitchMoment,
        0.0f, rear, 0.0f, 0, TRUE, TRUE, TRUE, FALSE, TRUE);
  }

  const float clutchStep =
      clutchEngagement - s_state.previousClutchEngagement;
  const ULONGLONG now = GetTickCount64();
  if (gear != 0 && now - s_lastClunkAt > 240 &&
      (std::fabs(clutchStep) > 0.28f ||
       std::fabs(s_state.twistVelocity) > 2.1f)) {
    DrivingEventBus::EventData event{};
    event.vehicle = vehicle;
    event.severity = Clamp01(
        std::fabs(clutchStep) * 1.6f +
        std::fabs(s_state.twistVelocity) * 0.18f);
    event.value = s_state.drivelineTwist;
    DrivingEventBus::Publish(
        DrivingEventBus::Event::TransmissionClunk, event);
    DrivingEventBus::Publish(
        DrivingEventBus::Event::DrivetrainFlex, event);
    s_lastClunkAt = now;
  }
  s_state.previousClutchEngagement = clutchEngagement;
}

float GetTorqueTransfer() {
  return s_state.torqueTransfer;
}

const State &GetState() {
  return s_state;
}

} // namespace VehicleDynamics
