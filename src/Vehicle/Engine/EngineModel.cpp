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

static float ResolveFlatVelocity(Vehicle vehicle, VehicleData &data,
                                 int maxGear) {
  const float memoryValue = std::fabs(data.GetDriveMaxFlatVel());
  if (std::isfinite(memoryValue) && memoryValue > 1.0f)
    return memoryValue;

  const float estimatedTop =
      std::max(1.0f, VEHICLE::GET_VEHICLE_ESTIMATED_MAX_SPEED(vehicle));
  const float topRatio =
      maxGear > 0
          ? std::fabs(data.GetGearRatio(static_cast<uint8_t>(maxGear)))
          : 0.0f;
  return topRatio > 0.01f ? estimatedTop * topRatio : estimatedTop;
}

static float ResolveWheelRPM(Vehicle vehicle, VehicleData &data, int gear,
                             int maxGear, float speedMps, float idleRPM) {
  if (gear == 0)
    return idleRPM;
  const uint8_t ratioIndex =
      gear < 0 ? 0 : static_cast<uint8_t>(gear);
  const float ratio = std::fabs(data.GetGearRatio(ratioIndex));
  const float flatVelocity = ResolveFlatVelocity(vehicle, data, maxGear);
  if (ratio <= 0.01f || flatVelocity <= 1.0f)
    return idleRPM;
  return std::clamp(std::fabs(speedMps) * ratio / flatVelocity, idleRPM,
                    1.25f);
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
  const float maxFlatVel = ResolveFlatVelocity(vehicle, data, maxGear);
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
  if (s_state.handlingBacked) {
    s_state.inertia = inertia;
  } else {
    const float nativeAcceleration =
        std::max(0.01f, VEHICLE::GET_VEHICLE_ACCELERATION(vehicle));
    s_state.inertia =
        std::clamp(0.72f + nativeAcceleration * 1.10f, 0.65f, 1.60f);
  }

  const bool open = gear == 0 || clutchDisengagement > 0.35f;
  s_state.freeRevActive = engineOn && open;

  s_state.estimatedFlatVelocity =
      ResolveFlatVelocity(vehicle, data, maxGear);
  s_state.wheelRPM =
      ResolveWheelRPM(vehicle, data, gear, maxGear, speedMps,
                      s_state.idleRPM);
  s_state.expectedRPM = open ? rpm : std::min(1.0f, s_state.wheelRPM);

  if (!engineOn) {
    s_state.rpmOwned = false;
    s_state.controlledRPM = 0.0f;
  } else if (open) {
    if (!s_state.rpmOwned) {
      s_state.controlledRPM =
          std::clamp(std::max(rpm, s_state.idleRPM), s_state.idleRPM, 1.0f);
      s_state.rpmOwned = true;
    }

    // GTA memutus tenaga di field clutch, tapi tidak free-rev konsisten di
    // gear 2+. Di fase terbuka saja kita lanjutkan state RPM mesin memakai
    // inertia kendaraan; kecepatan roda tidak pernah ditulis.
    const float pedal = Clamp01(throttle);
    const float freeTarget =
        s_state.idleRPM +
        std::pow(pedal, 0.62f) * (1.0f - s_state.idleRPM);
    const float clutchDrag =
        gear == 0 ? 0.0f : Clamp01(clutchEngagement * clutchEngagement * 0.65f);
    const float target =
        freeTarget + (s_state.wheelRPM - freeTarget) * clutchDrag;
    const float inertiaScale = std::clamp(s_state.inertia, 0.30f, 3.0f);
    const float response =
        target >= s_state.controlledRPM
            ? 1.45f + inertiaScale * 1.65f
            : 0.85f + 1.15f / std::sqrt(inertiaScale);
    const float alpha = 1.0f - std::exp(-response * dt);

    if (pedal > 0.01f && rpm > s_state.controlledRPM)
      s_state.controlledRPM = std::min(rpm, 1.0f);
    s_state.controlledRPM +=
        (target - s_state.controlledRPM) * Clamp01(alpha);
    s_state.controlledRPM =
        std::clamp(s_state.controlledRPM, s_state.idleRPM, 1.0f);

    data.SetRPM(s_state.controlledRPM);
    data.SetThrottle(pedal);
    s_state.expectedRPM = s_state.controlledRPM;
  } else {
    s_state.rpmOwned = false;
    s_state.controlledRPM = rpm;
  }
  s_state.previousRPM = rpm;

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
