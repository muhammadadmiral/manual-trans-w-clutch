#include "EngineModel.h"
#include "../VehicleData.h"
#include "../../Core/Config.h"
#include "../../../sdk/inc/natives.h"
#include <algorithm>
#include <cmath>

namespace EngineModel {

static State s_state;

static float Clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

void Reset() {
  s_state = State{};
}

static bool UpdateLoadAndStall(Vehicle vehicle, VehicleData &data, int gear,
                               int maxGear,
                               float engagement, float throttle,
                               float brake, float speedMps, bool engineOn, float dt,
                               bool automaticMode) {
  if (!engineOn || gear == 0) {
    s_state.load = 0.0f;
    s_state.creepThrottle = 0.0f;
    s_state.torqueReserve = 0.0f;
    s_state.stallProgress =
        std::max(0.0f, s_state.stallProgress - dt * 3.0f);
    return false;
  }

  const uint8_t ratioIndex =
      gear < 0 ? 0 : static_cast<uint8_t>(gear);
  const float ratio = std::fabs(data.GetGearRatio(ratioIndex));
  const float maxFlatVel = std::fabs(data.GetDriveMaxFlatVel());
  const bool hasDrivelineData =
      std::isfinite(ratio) && ratio > 0.01f &&
      std::isfinite(maxFlatVel) && maxFlatVel > 1.0f;

  const float nativeTopSpeed =
      std::max(1.0f, VEHICLE::GET_VEHICLE_ESTIMATED_MAX_SPEED(vehicle));
  const float fallbackRatio =
      1.0f / static_cast<float>((std::max)(1, std::abs(gear)));
  const float effectiveRatio = hasDrivelineData ? ratio : fallbackRatio;
  const float effectiveTopSpeed = hasDrivelineData ? maxFlatVel : nativeTopSpeed;
  const float idleRoadSpeed =
      hasDrivelineData
          ? s_state.idleRPM * effectiveTopSpeed / effectiveRatio
          : s_state.idleRPM * effectiveTopSpeed *
                static_cast<float>((std::max)(1, std::abs(gear))) /
                static_cast<float>((std::max)(1, maxGear));
  const float directionalSpeed =
      gear < 0 ? -speedMps : speedMps;
  const float usefulSpeed = std::max(0.0f, directionalSpeed);
  const float speedGap =
      idleRoadSpeed > 0.01f
          ? Clamp01((idleRoadSpeed - usefulSpeed) / idleRoadSpeed)
          : 0.0f;
  s_state.load = engagement * speedGap;

  s_state.creepThrottle = 0.0f;
  if (Config::IdleCreep && std::abs(gear) == 1 && throttle < 0.02f &&
      brake < 0.05f &&
      engagement > 0.50f && speedGap > 0.01f) {
    s_state.creepThrottle =
        Clamp01(Config::IdleCreepThrottle) * speedGap * engagement;
    const int driveControl = gear < 0 ? 72 : 71;
    PAD::DISABLE_CONTROL_ACTION(0, driveControl, true);
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(
        0, driveControl, s_state.creepThrottle);
  }

  const float firstRatio = std::fabs(data.GetGearRatio(1));
  const float gearLeverage =
      firstRatio > 0.01f && hasDrivelineData
          ? Clamp01(effectiveRatio / firstRatio)
          : fallbackRatio;
  const float driveForce = data.GetDriveForce();
  const float nativeAcceleration =
      std::max(0.01f, VEHICLE::GET_VEHICLE_ACCELERATION(vehicle));
  const float driveScale =
      std::isfinite(driveForce) && driveForce > 0.01f
          ? std::clamp(driveForce / 0.30f, 0.35f, 2.0f)
          : std::clamp(nativeAcceleration / 0.30f, 0.45f, 1.80f);
  const float effectiveThrottle =
      std::max(Clamp01(throttle), s_state.creepThrottle);
  const float idleTorque =
      std::clamp(Config::IdleTorqueFraction, 0.02f, 0.60f);
  const float availableTorque =
      (idleTorque + effectiveThrottle * (1.0f - idleTorque)) *
      driveScale * gearLeverage;
  const float torqueDemand = engagement * speedGap;
  s_state.torqueReserve = availableTorque - torqueDemand;

  const float rpm = data.GetRPM();
  const float stallClutch =
      std::clamp(Config::StallClutchThreshold, 0.30f, 0.95f);
  const float uphillLoad =
      Clamp01(std::max(0.0f, ENTITY::GET_ENTITY_PITCH(vehicle)) / 18.0f);
  const bool bogging = !automaticMode && Config::StallEnabled &&
                       engagement > stallClutch &&
                       rpm <= s_state.idleRPM + 0.035f &&
                       usefulSpeed < idleRoadSpeed &&
                       (s_state.torqueReserve - uphillLoad * 0.35f) < 0.0f;
  if (bogging) {
    const float clutchLoad =
        Clamp01((engagement - stallClutch) / (1.0f - stallClutch));
    const float torqueDeficit =
        Clamp01(-(s_state.torqueReserve - uphillLoad * 0.35f));
    const float brakeLoad = 1.0f + Clamp01(brake) * 1.25f;
    const float highGearLoad =
        1.0f + 0.30f * static_cast<float>(
                          (std::max)(0, std::abs(gear) - 1));
    s_state.stallProgress +=
        clutchLoad * speedGap * torqueDeficit * brakeLoad * highGearLoad * dt *
        std::max(0.10f, Config::StallRate);
  } else {
    s_state.stallProgress =
        std::max(0.0f, s_state.stallProgress - dt * 3.0f);
  }

  if (s_state.stallProgress >= 1.0f) {
    s_state.stallProgress = 0.0f;
    return true;
  }
  return false;
}

bool Update(Vehicle vehicle, VehicleData &data, int gear, int maxGear,
            float clutchDisengagement, float clutchEngagement,
            float throttle, float brake, float speedMps, bool engineOn,
            bool automaticMode) {
  const float dt = std::clamp(MISC::GET_FRAME_TIME(), 0.001f, 0.05f);
  const float rpm = data.GetRPM();

  if (engineOn && throttle < 0.01f && rpm > 0.05f && rpm < 0.35f)
    s_state.idleRPM += (rpm - s_state.idleRPM) * Clamp01(dt * 2.0f);

  const float inertia = data.GetDriveInertia();
  s_state.handlingBacked =
      std::isfinite(inertia) && inertia >= 0.05f && inertia <= 10.0f;
  s_state.inertia = s_state.handlingBacked ? inertia : 1.0f;

  const bool open = gear == 0 || clutchDisengagement > 0.40f;
  s_state.freeRevActive = engineOn && open;
  s_state.previousRPM = rpm;

  // expectedRPM cuma telemetry. Jangan tulis RPM/throttle internal: audio,
  // limiter, inertia, dan perbedaan handling kendaraan harus tetap milik GTA.
  const uint8_t ratioIndex =
      gear < 0 ? 0 : static_cast<uint8_t>(gear);
  const float connectedRatio = std::fabs(data.GetGearRatio(ratioIndex));
  const float maxFlatVel = std::fabs(data.GetDriveMaxFlatVel());
  if (!open && connectedRatio > 0.01f && maxFlatVel > 1.0f) {
    s_state.expectedRPM = std::clamp(
        std::max(s_state.idleRPM,
                 std::fabs(speedMps) * connectedRatio / maxFlatVel),
        s_state.idleRPM, 1.0f);
  } else {
    s_state.expectedRPM = rpm;
  }

  const float ratio =
      gear > 0 ? std::fabs(data.GetGearRatio(static_cast<uint8_t>(gear)))
               : 0.0f;
  const float top =
      maxGear > 0
          ? std::fabs(data.GetGearRatio(static_cast<uint8_t>(maxGear)))
          : 0.0f;
  if (engineOn && gear > 0 && clutchEngagement > 0.6f &&
      throttle < 0.05f) {
    const float nativeTop =
        std::max(1.0f, VEHICLE::GET_VEHICLE_ESTIMATED_MAX_SPEED(vehicle));
    const float ratioLoad =
        ratio > 0.01f && top > 0.01f
            ? Clamp01(ratio / top / static_cast<float>((std::max)(1, maxGear)))
            : Clamp01(static_cast<float>((std::max)(1, maxGear)) /
                      static_cast<float>((std::max)(1, gear)) / 3.0f);
    const float rpmOverrun =
        Clamp01((rpm - s_state.idleRPM) /
                std::max(0.05f, 1.0f - s_state.idleRPM));
    const float roadLoad =
        Clamp01(std::fabs(speedMps) / nativeTop);
    const float target =
        Clamp01(rpmOverrun * (0.45f + ratioLoad * 0.55f) +
                roadLoad * ratioLoad * 0.35f);
    s_state.engineBrake +=
        (target - s_state.engineBrake) * Clamp01(dt * 8.0f);
  } else {
    s_state.engineBrake +=
        (0.0f - s_state.engineBrake) * Clamp01(dt * 8.0f);
  }

  return UpdateLoadAndStall(
      vehicle, data, gear, maxGear, clutchEngagement, throttle, brake,
      speedMps, engineOn, dt, automaticMode);
}

float GetLoad() { return s_state.load; }
float GetStallProgress() { return s_state.stallProgress; }
float GetEngineBrake() { return s_state.engineBrake; }
float GetInertia() { return s_state.inertia; }
float GetExpectedRPM() { return s_state.expectedRPM; }
float GetCreepThrottle() { return s_state.creepThrottle; }
float GetTorqueReserve() { return s_state.torqueReserve; }
const State &GetState() { return s_state; }

} // namespace EngineModel
