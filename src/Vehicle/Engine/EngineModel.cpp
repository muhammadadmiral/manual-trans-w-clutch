#include "EngineModel.h"

#include "../DrivetrainKinematics.h"
#include "../VehicleData.h"
#include "../Maintenance/WorkshopTuning.h"
#include "../../Core/Config.h"
#include "../../../sdk/inc/natives.h"

#include <algorithm>
#include <cmath>

namespace EngineModel {
namespace {

State s_state;

float Clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

bool IsFinitePositive(float value) {
  return std::isfinite(value) && value > 0.0f;
}

bool IsLaunchRatio(VehicleData &data, int gear, int maxGear) {
  if (gear < 0)
    return IsFinitePositive(std::fabs(data.GetGearRatio(0)));
  if (gear < 1 || maxGear < 1)
    return false;

  const float selected =
      std::fabs(data.GetGearRatio(static_cast<uint8_t>(gear)));
  float largestForwardRatio = 0.0f;
  for (int candidate = 1; candidate <= maxGear; ++candidate) {
    const float ratio =
        std::fabs(data.GetGearRatio(static_cast<uint8_t>(candidate)));
    if (IsFinitePositive(ratio))
      largestForwardRatio = std::max(largestForwardRatio, ratio);
  }
  return IsFinitePositive(selected) &&
         IsFinitePositive(largestForwardRatio) &&
         selected >= largestForwardRatio * 0.98f;
}

void ObserveNativeIdle(float rpm, float throttle, bool engineOn,
                       bool drivelineOpen, float dt) {
  if (!engineOn)
    return;

  const bool stableIdle =
      drivelineOpen && throttle <= 0.02f && std::isfinite(rpm) &&
      rpm > 0.01f && rpm < 1.25f;
  if (!stableIdle) {
    s_state.idleCalibrationTime =
        std::max(0.0f, s_state.idleCalibrationTime - dt * 0.25f);
    return;
  }

  if (s_state.idleRPM <= 0.0f) {
    s_state.idleRPM = rpm;
    s_state.idleDeviation = 0.0f;
  } else {
    const float error = rpm - s_state.idleRPM;
    const float learnRate = s_state.idleCalibrated ? 0.35f : 4.0f;
    const float alpha = 1.0f - std::exp(-learnRate * dt);
    s_state.idleRPM += error * alpha;
    s_state.idleDeviation +=
        (std::fabs(error) - s_state.idleDeviation) * alpha;
  }

  s_state.idleCalibrationTime += dt;
  s_state.idleCalibrated = s_state.idleCalibrationTime >= 0.35f;
  const float nativeRipple =
      std::max(0.01f, s_state.idleDeviation * 4.0f);
  s_state.minimumRunningRPM =
      std::max(0.0f, s_state.idleRPM - nativeRipple);
}

bool UpdateEnvironment(Vehicle vehicle, bool engineOn, float dt) {
  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
  const bool electric = VEHICLE::_GET_IS_VEHICLE_ELECTRIC(model) != FALSE;
  s_state.airborne = ENTITY::IS_ENTITY_IN_AIR(vehicle) != FALSE;
  s_state.upsideDown =
      ENTITY::IS_ENTITY_UPSIDEDOWN(vehicle) != FALSE ||
      VEHICLE::IS_VEHICLE_STUCK_ON_ROOF(vehicle) != FALSE;
  s_state.environmentStall = false;

  if (!engineOn || electric) {
    s_state.waterIngestion =
        std::max(0.0f, s_state.waterIngestion - dt * 2.0f);
    s_state.oilStarvation =
        std::max(0.0f, s_state.oilStarvation - dt * 2.0f);
    return false;
  }

  const float submerged =
      std::clamp(ENTITY::GET_ENTITY_SUBMERGED_LEVEL(vehicle), 0.0f, 1.0f);
  if (submerged > 0.58f) {
    const float delay = std::clamp(Config::WaterStallDelay, 0.50f, 12.0f);
    s_state.waterIngestion +=
        dt * (0.35f + submerged * 0.90f) / delay;
  } else {
    s_state.waterIngestion =
        std::max(0.0f, s_state.waterIngestion - dt * 0.75f);
  }

  if (s_state.upsideDown) {
    const float delay =
        std::clamp(Config::RolloverStallDelay, 1.0f, 20.0f);
    s_state.oilStarvation += dt / delay;
  } else {
    s_state.oilStarvation =
        std::max(0.0f, s_state.oilStarvation - dt * 0.45f);
  }

  if (s_state.waterIngestion >= 1.0f ||
      s_state.oilStarvation >= 1.0f) {
    s_state.waterIngestion = std::min(s_state.waterIngestion, 1.0f);
    s_state.oilStarvation = std::min(s_state.oilStarvation, 1.0f);
    s_state.environmentStall = true;
    return true;
  }
  return false;
}

} // namespace

void Reset() {
  s_state = State{};
  s_state.driveTorqueFactor = 1.0f;
}

float PrepareIdleDrive(Vehicle vehicle, VehicleData &data, int gear,
                       int maxGear, float engagement, float throttle,
                       float brake, float speedMps, bool engineOn,
                       bool automaticMode, bool gentleClutchRelease) {
  s_state.creepThrottle = 0.0f;
  s_state.hillRollback = false;

  if (!Config::IdleCreep || !engineOn || gear == 0 ||
      !s_state.idleCalibrated || throttle > 0.02f ||
      engagement <= 0.0f || brake >= 1.0f ||
      !IsLaunchRatio(data, gear, maxGear)) {
    return 0.0f;
  }

  const auto native =
      DrivetrainKinematics::Resolve(vehicle, data, gear, maxGear);
  if (!native.ratioSetValid || !native.memoryFlatVelocityValid ||
      !IsFinitePositive(native.gearLimitSpeedMps)) {
    return 0.0f;
  }

  const float nativeIdleRoadSpeed =
      s_state.idleRPM * native.gearLimitSpeedMps;
  if (!IsFinitePositive(nativeIdleRoadSpeed))
    return 0.0f;

  const float directionalSpeed = gear < 0 ? -speedMps : speedMps;
  const float speedGap =
      Clamp01((nativeIdleRoadSpeed - std::max(0.0f, directionalSpeed)) /
              nativeIdleRoadSpeed);
  const float brakeRelease = 1.0f - Clamp01(brake);
  const float releaseGate =
      automaticMode || gentleClutchRelease ? 1.0f : 0.0f;
  const float configuredPedal =
      Clamp01(Config::IdleCreepThrottle) *
      WorkshopTuning::GetCreepTorqueMultiplier();

  s_state.creepThrottle =
      Clamp01(configuredPedal * Clamp01(engagement) * speedGap *
              brakeRelease * releaseGate);
  return s_state.creepThrottle;
}

bool Update(Vehicle vehicle, VehicleData &data, int gear, int maxGear,
            float clutchDisengagement, float clutchEngagement,
            float throttle, float brake, float speedMps, bool engineOn,
            bool automaticMode) {
  const float dt = std::clamp(MISC::GET_FRAME_TIME(), 0.001f, 0.05f);
  const float nativeRPM = data.GetRPM();
  const float rpm =
      std::isfinite(nativeRPM) ? std::max(0.0f, nativeRPM) : 0.0f;
  const bool drivelineOpen =
      gear == 0 || clutchDisengagement >= 0.88f;
  ObserveNativeIdle(rpm, Clamp01(throttle), engineOn, drivelineOpen, dt);

  s_state.rpmOwned = false;
  s_state.freeRevActive = engineOn && drivelineOpen;
  s_state.controlledRPM = rpm;
  s_state.expectedRPM = rpm;
  s_state.previousRPM = rpm;
  s_state.previousThrottle = Clamp01(throttle);
  s_state.driveTorqueFactor = 1.0f;
  s_state.lowRpmRecovery = 0.0f;
  s_state.redlineCut = false;
  s_state.nativeCutRecovered = false;

  const float inertia = data.GetDriveInertia();
  s_state.handlingBacked =
      std::isfinite(inertia) && inertia > 0.0f;
  s_state.inertia =
      s_state.handlingBacked
          ? inertia * WorkshopTuning::GetFlywheelInertiaMultiplier()
          : 0.0f;

  const auto native =
      DrivetrainKinematics::Resolve(vehicle, data, gear, maxGear);
  s_state.initialDriveMaxFlatVel =
      native.memoryFlatVelocityValid ? native.flatVelocity : 0.0f;
  s_state.estimatedFlatVelocity = s_state.initialDriveMaxFlatVel;
  s_state.gearLimitSpeedMps =
      native.memoryFlatVelocityValid ? native.gearLimitSpeedMps : 0.0f;
  s_state.adaptiveGearing = false;
  const float directionalRoadSpeed =
      gear < 0 ? -speedMps : speedMps;
  s_state.wheelRPM =
      gear != 0 && native.ratioSetValid &&
              native.memoryFlatVelocityValid &&
              IsFinitePositive(native.gearLimitSpeedMps)
          ? std::max(0.0f, directionalRoadSpeed) /
                native.gearLimitSpeedMps
          : rpm;
  s_state.connectedRPMTarget = s_state.wheelRPM;
  s_state.drivenWheelSpeedMps = std::fabs(speedMps);
  s_state.wheelTelemetryValid = false;
  s_state.burnoutActive = false;

  // GTA/CVehicle only exposes normalized rev ratio. Do not invent a physical
  // redline or RPM scale from vehicle class, speed or tire size.
  s_state.estimatedEngineRPM = 0.0f;
  s_state.estimatedIdlePhysicalRPM = 0.0f;
  s_state.estimatedRedlineRPM = 0.0f;
  s_state.redlineHandlingBacked = false;

  const float directionalSpeed = gear < 0 ? -speedMps : speedMps;
  if (!s_state.speedSampleValid) {
    s_state.previousDirectionalSpeed = directionalSpeed;
    s_state.longitudinalAcceleration = 0.0f;
    s_state.speedSampleValid = true;
  } else {
    const float rawAcceleration =
        (directionalSpeed - s_state.previousDirectionalSpeed) / dt;
    const float alpha = 1.0f - std::exp(-5.0f * dt);
    s_state.longitudinalAcceleration +=
        (rawAcceleration - s_state.longitudinalAcceleration) * alpha;
    s_state.previousDirectionalSpeed = directionalSpeed;
  }

  const bool environmentStall =
      UpdateEnvironment(vehicle, engineOn, dt);
  if (!engineOn || gear == 0 || automaticMode ||
      !s_state.idleCalibrated) {
    s_state.load = 0.0f;
    s_state.lugSeverity = 0.0f;
    s_state.torqueReserve = 0.0f;
    s_state.stallProgress =
        std::max(0.0f, s_state.stallProgress - dt * 3.0f);
    return environmentStall;
  }

  const float coupledRPM =
      native.ratioSetValid && native.memoryFlatVelocityValid
          ? rpm + (s_state.wheelRPM - rpm) * Clamp01(clutchEngagement)
          : rpm;
  const float minimum =
      std::max(0.001f, s_state.minimumRunningRPM);
  s_state.load =
      Clamp01((s_state.idleRPM - coupledRPM) /
              std::max(0.001f, s_state.idleRPM)) *
      Clamp01(clutchEngagement);
  s_state.lugSeverity =
      Clamp01((minimum - coupledRPM) / minimum);
  s_state.torqueReserve = coupledRPM - minimum;
  s_state.torqueCurve = 1.0f;
  s_state.engineCondition =
      std::clamp(VEHICLE::GET_VEHICLE_ENGINE_HEALTH(vehicle) / 1000.0f,
                 0.0f, 1.0f);

  const float stallClutch =
      std::clamp(Config::StallClutchThreshold, 0.30f, 0.95f);
  const bool belowNativeMinimum =
      Config::StallEnabled && clutchEngagement >= stallClutch &&
      coupledRPM < minimum && !s_state.airborne &&
      !s_state.upsideDown;
  if (belowNativeMinimum) {
    const float severity =
        Clamp01((minimum - coupledRPM) / minimum);
    s_state.stallProgress +=
        dt * std::max(0.10f, Config::StallRate) *
        (1.0f + severity * 3.0f);
  } else {
    s_state.stallProgress =
        std::max(0.0f, s_state.stallProgress - dt * 3.0f);
  }

  if (s_state.stallProgress >= 1.0f) {
    s_state.stallProgress = 0.0f;
    return true;
  }
  return environmentStall;
}

float GetLoad() { return s_state.load; }
float GetStallProgress() { return s_state.stallProgress; }
float GetEngineBrake() { return s_state.engineBrake; }
float GetInertia() { return s_state.inertia; }
float GetExpectedRPM() { return s_state.expectedRPM; }
float GetCreepThrottle() { return s_state.creepThrottle; }
float GetTorqueReserve() { return s_state.torqueReserve; }
float GetDriveTorqueFactor() { return s_state.driveTorqueFactor; }
const State &GetState() { return s_state; }

} // namespace EngineModel
