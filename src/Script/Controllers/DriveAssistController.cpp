// =============================================================================
// DriveAssistController.cpp
//
// ESC uses body-frame lateral velocity, yaw-rate error and slip angle. GTA's
// public native API has no per-wheel brake-pressure setter, so selective
// braking is represented by a conservative rear/front-corner braking impulse.
// This creates the same corrective yaw moment without unsafe CWheel writes.
// =============================================================================
#include "DriveAssistController.h"

#include "../../../sdk/inc/natives.h"
#include "../../Core/Config.h"
#include "../../Core/InputHandler.h"
#include "../../Core/ModLogger.h"
#include "../../Vehicle/Brakes/BrakeSystem.h"
#include "../../Vehicle/Brakes/ParkingBrake.h"
#include "../../Vehicle/Engine/LaunchControl.h"
#include "../../Vehicle/Engine/TractionControl.h"
#include "../DrivingEventBus.h"


#include <algorithm>
#include <cmath>

namespace {

constexpr float kRadiansToDegrees = 57.2957795f;

float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

float SmoothToward(float current, float target, float rate, float dt) {
  const float alpha = 1.0f - std::exp(-std::max(0.0f, rate) * dt);
  return current + (target - current) * Clamp01(alpha);
}

} // namespace

void DriveAssistController::Reset() {
  m_state = AssistState{};
  m_vehicle = 0;
  m_previousLateralVelocity = 0.0f;
  m_filteredLateralAcceleration = 0.0f;
  m_filteredSlipAngle = 0.0f;
  m_velocityInitialized = false;
  m_tcsWasActive = false;
  m_absWasActive = false;
  m_escWasActive = false;
  m_lcWasArmed = false;
  m_lcWasLimiting = false;
  m_rollWasActive = false;
  TractionControl::Reset();
  BrakeSystem::Reset();
  LaunchControl::Reset();
}

void DriveAssistController::ApplySelectiveBrake(Vehicle veh, float severity,
                                                float yawError,
                                                bool oversteer) {
  if (severity <= 0.01f)
    return;

  Vector3 minimum{};
  Vector3 maximum{};
  MISC::GET_MODEL_DIMENSIONS(ENTITY::GET_ENTITY_MODEL(veh), &minimum, &maximum);
  const float halfWidth =
      std::clamp((maximum.x - minimum.x) * 0.42f, 0.35f, 1.30f);
  const float frontY = std::clamp(maximum.y * 0.72f, 0.45f, 2.20f);
  const float rearY = std::clamp(minimum.y * 0.72f, -2.20f, -0.45f);

  // Oversteer: brake the outer front corner to oppose excessive yaw.
  // Understeer: brake the inner rear corner to help the car rotate.
  const bool yawingRight = yawError < 0.0f;
  float offsetX = 0.0f;
  float offsetY = 0.0f;
  if (oversteer) {
    offsetX = yawingRight ? -halfWidth : halfWidth;
    offsetY = frontY;
    m_state.brakeCorner =
        yawingRight ? BrakeCorner::FrontLeft : BrakeCorner::FrontRight;
  } else {
    offsetX = yawingRight ? halfWidth : -halfWidth;
    offsetY = rearY;
    m_state.brakeCorner =
        yawingRight ? BrakeCorner::RearRight : BrakeCorner::RearLeft;
  }

  // Force is intentionally small. The normal brake request does most of the
  // deceleration; this off-centre impulse only supplies corrective yaw.
  const float brakingForce =
      std::clamp(severity * Config::EscBrakeStrength, 0.0f, 1.0f) * 0.16f;
  ENTITY::APPLY_FORCE_TO_ENTITY(veh, 1, 0.0f, -brakingForce, 0.0f, offsetX,
                                offsetY, minimum.z + 0.12f, 0, TRUE, TRUE, TRUE,
                                FALSE, TRUE);
}

void DriveAssistController::Update(Vehicle veh, VehicleData &data, int gear,
                                   float clutchDisengagement, float &throttle,
                                   float &brake, float forwardSpeed,
                                   bool engineOn, bool automaticMode) {
  if (veh != m_vehicle) {
    Reset();
    m_vehicle = veh;
  }

  const float dt = std::clamp(MISC::GET_FRAME_TIME(), 0.001f, 0.05f);
  throttle = Clamp01(throttle);
  brake = Clamp01(brake);

  TractionControl::Update(veh, data, forwardSpeed, gear, clutchDisengagement,
                          throttle);
  brake = BrakeSystem::UpdateABS(veh, data, brake, forwardSpeed, gear < 0);

  const auto localVelocity = ENTITY::GET_ENTITY_SPEED_VECTOR(veh, TRUE);
  const auto angularVelocity = ENTITY::GET_ENTITY_ROTATION_VELOCITY(veh);
  const float lateralVelocity = localVelocity.x;
  const float absoluteForwardSpeed = std::fabs(forwardSpeed);
  const float rawSlipAngle =
      std::atan2(lateralVelocity, std::max(1.0f, absoluteForwardSpeed)) *
      kRadiansToDegrees;
  if (!m_velocityInitialized) {
    m_previousLateralVelocity = lateralVelocity;
    m_velocityInitialized = true;
  }

  // Body-frame acceleration. The yaw term prevents a normal steady corner
  // from looking like a zero-acceleration slide.
  const float lateralDerivative =
      (lateralVelocity - m_previousLateralVelocity) / dt;
  const float rawLateralAcceleration =
      lateralDerivative - angularVelocity.z * forwardSpeed;
  m_previousLateralVelocity = lateralVelocity;
  m_filteredLateralAcceleration = SmoothToward(
      m_filteredLateralAcceleration, rawLateralAcceleration, 8.0f, dt);
  m_filteredSlipAngle =
      SmoothToward(m_filteredSlipAngle, rawSlipAngle, 10.0f, dt);

  Vector3 minimum{};
  Vector3 maximum{};
  MISC::GET_MODEL_DIMENSIONS(ENTITY::GET_ENTITY_MODEL(veh), &minimum, &maximum);
  const float wheelbase =
      std::clamp((maximum.y - minimum.y) * 0.62f, 1.55f, 4.20f);
  const float steering = InputHandler::GetSmoothedSteer();
  const float desiredYaw =
      std::clamp(std::tan(steering * 0.48f) * absoluteForwardSpeed / wheelbase,
                 -1.8f, 1.8f);
  const float yawError = angularVelocity.z - desiredYaw;

  const float slipThreshold =
      std::clamp(Config::EscSlipAngleThresholdDeg, 2.0f, 20.0f);
  const float slipSeverity =
      Clamp01((std::fabs(m_filteredSlipAngle) - slipThreshold) /
              std::max(4.0f, 26.0f - slipThreshold));
  const float yawThreshold = 0.18f + 0.012f * absoluteForwardSpeed;
  const float yawSeverity =
      Clamp01((std::fabs(yawError) - yawThreshold) / 1.20f);
  const float lateralG = std::fabs(m_filteredLateralAcceleration) / 9.80665f;
  const float lateralSeverity = Clamp01((lateralG - 0.38f) / 0.72f);
  const bool escAllowed = Config::EscEnabled && engineOn && gear != 0 &&
                          absoluteForwardSpeed * 3.6f >=
                              std::clamp(Config::EscMinSpeedKmH, 5.0f, 80.0f) &&
                          !ENTITY::IS_ENTITY_IN_AIR(veh);
  const float requestedEsc =
      escAllowed ? std::max(slipSeverity, yawSeverity * slipSeverity *
                                              (0.45f + lateralSeverity * 0.55f))
                 : 0.0f;
  m_state.stabilityError =
      SmoothToward(m_state.stabilityError, requestedEsc,
                   requestedEsc > m_state.stabilityError ? 12.0f : 5.0f, dt);
  m_state.escActive = escAllowed && m_state.stabilityError > 0.04f;
  m_state.brakeCorner = BrakeCorner::None;
  m_state.escBrake = 0.0f;
  m_state.escThrottleCut = 0.0f;
  if (m_state.escActive) {
    m_state.escThrottleCut =
        Clamp01(Config::EscMaxThrottleCut) * m_state.stabilityError;
    throttle *= 1.0f - m_state.escThrottleCut;
    m_state.escBrake =
        Clamp01(Config::EscBrakeStrength) * m_state.stabilityError;
    brake = std::max(brake, m_state.escBrake);
    const bool oversteer = std::fabs(angularVelocity.z) > std::fabs(desiredYaw);
    ApplySelectiveBrake(veh, m_state.stabilityError, yawError, oversteer);
  }

  const float rollAngle = std::fabs(ENTITY::GET_ENTITY_ROLL(veh));
  const float rollThreshold =
      std::clamp(Config::RolloverWarningAngleDeg, 15.0f, 75.0f);
  m_state.rollWarning = Config::RolloverAssist && absoluteForwardSpeed > 3.0f &&
                        !ENTITY::IS_ENTITY_IN_AIR(veh) &&
                        rollAngle >= rollThreshold;
  if (m_state.rollWarning) {
    const float rollSeverity = Clamp01((rollAngle - rollThreshold) /
                                       std::max(5.0f, 80.0f - rollThreshold));
    throttle *= 1.0f - rollSeverity * 0.72f;
    brake = std::max(brake, rollSeverity * 0.28f);
  }

  LaunchControl::Update(data, gear, clutchDisengagement, throttle, brake,
                        forwardSpeed, engineOn, automaticMode);

  m_state.tcsActive = TractionControl::IsTCSActive();
  m_state.absActive = BrakeSystem::IsABSActive();
  m_state.lcArmed = LaunchControl::GetState().armed;
  m_state.launchCut = LaunchControl::GetState().cutLevel;
  m_state.hillHoldActive = ParkingBrake::IsHillHoldActive();
  m_state.tcsThrottle = throttle;
  m_state.absBrake = brake;
  m_state.torqueIntervention = std::max(
      std::max(TractionControl::GetState().cutLevel, m_state.escThrottleCut),
      m_state.launchCut);
  m_state.lateralVelocity = lateralVelocity;
  m_state.lateralAcceleration = m_filteredLateralAcceleration;
  m_state.slipAngleDeg = m_filteredSlipAngle;
  m_state.yawRate = angularVelocity.z;
  m_state.desiredYawRate = desiredYaw;
  m_state.rollAngleDeg = rollAngle;

  DrivingEventBus::EventData eventData{};
  eventData.vehicle = veh;
  if (m_state.tcsActive && !m_tcsWasActive) {
    eventData.severity = TractionControl::GetState().cutLevel;
    DrivingEventBus::Publish(DrivingEventBus::Event::TCSActivated, eventData);
    DrivingEventBus::Publish(DrivingEventBus::Event::TCSCut, eventData);
  }
  if (m_state.absActive && !m_absWasActive) {
    eventData.severity = BrakeSystem::GetState().absLevel;
    DrivingEventBus::Publish(DrivingEventBus::Event::ABSActivated, eventData);
    DrivingEventBus::Publish(DrivingEventBus::Event::ABSPulse, eventData);
  }
  if (m_state.escActive && !m_escWasActive) {
    eventData.severity = m_state.stabilityError;
    DrivingEventBus::Publish(DrivingEventBus::Event::ESCActivated, eventData);
  }
  if (m_state.lcArmed && !m_lcWasArmed)
    DrivingEventBus::Publish(DrivingEventBus::Event::LaunchControlArmed,
                             eventData);
  const bool launchLimiting = LaunchControl::GetState().limiting;
  if (launchLimiting && !m_lcWasLimiting) {
    eventData.severity = LaunchControl::GetState().cutLevel;
    DrivingEventBus::Publish(DrivingEventBus::Event::LaunchControlCut,
                             eventData);
  }
  if (m_state.rollWarning && !m_rollWasActive) {
    eventData.severity = Clamp01((rollAngle - rollThreshold) /
                                 std::max(5.0f, 80.0f - rollThreshold));
    eventData.value = rollAngle;
    DrivingEventBus::Publish(DrivingEventBus::Event::RolloverWarning,
                             eventData);
    LOG_WARN(Physics, "Rollover warning: roll=%.1f speed=%.1fkm/h", rollAngle,
             absoluteForwardSpeed * 3.6f);
  }
  m_tcsWasActive = m_state.tcsActive;
  m_absWasActive = m_state.absActive;
  m_escWasActive = m_state.escActive;
  m_lcWasArmed = m_state.lcArmed;
  m_lcWasLimiting = launchLimiting;
  m_rollWasActive = m_state.rollWarning;
}
