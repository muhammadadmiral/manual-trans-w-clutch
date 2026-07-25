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

static void UpdateFreeRev(VehicleData &data, bool drivelineOpen,
                          float throttle, bool engineOn, float dt) {
  const float rpm = data.GetRPM();
  if (!engineOn || !drivelineOpen) {
    s_state.freeRevActive = false;
    s_state.previousRPM = rpm;
    return;
  }

  if (!s_state.freeRevActive) {
    s_state.previousRPM = rpm;
    s_state.freeRevActive = true;
  }

  data.SetThrottle(throttle > 0.001f ? 1.0f : 0.0f);
  data.SetThrottlePedal(throttle);

  if (throttle <= 0.001f) {
    s_state.previousRPM = rpm;
    return;
  }

  // GTA gak free-rev konsisten saat clutch kebuka. Tambah RPM pakai inertia
  // handling kendaraan, sementara drag dan limiter tetap milik engine GTA.
  const float rpmDrop =
      s_state.previousRPM > rpm ? s_state.previousRPM - rpm : 0.0f;
  const float next =
      Clamp01(rpm + rpmDrop + throttle * 2.0f * s_state.inertia * dt);
  data.SetRPM(next);
  s_state.previousRPM = next;
}

static bool UpdateLoadAndStall(VehicleData &data, int gear,
                               float engagement, float throttle,
                               float speedMps, bool engineOn, float dt) {
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

  if (!hasDrivelineData) {
    s_state.load = 0.0f;
    s_state.stallProgress =
        std::max(0.0f, s_state.stallProgress - dt * 3.0f);
    return false;
  }

  const float idleRoadSpeed = s_state.idleRPM * maxFlatVel / ratio;
  const float directionalSpeed =
      gear < 0 ? -speedMps : speedMps;
  const float usefulSpeed = std::max(0.0f, directionalSpeed);
  const float speedGap =
      idleRoadSpeed > 0.01f
          ? Clamp01((idleRoadSpeed - usefulSpeed) / idleRoadSpeed)
          : 0.0f;
  s_state.load = engagement * speedGap;

  s_state.creepThrottle = 0.0f;
  if (Config::IdleCreep && gear == 1 && throttle < 0.02f &&
      engagement > 0.50f && speedGap > 0.01f) {
    s_state.creepThrottle =
        Clamp01(Config::IdleCreepThrottle) * speedGap * engagement;
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 71, s_state.creepThrottle);
  }

  const float firstRatio = std::fabs(data.GetGearRatio(1));
  const float gearLeverage =
      firstRatio > 0.01f ? Clamp01(ratio / firstRatio) : 1.0f;
  const float driveForce = data.GetDriveForce();
  const float driveScale =
      std::isfinite(driveForce) && driveForce > 0.01f
          ? std::clamp(driveForce / 0.30f, 0.35f, 2.0f)
          : 1.0f;
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
  const bool bogging = Config::StallEnabled && engagement > stallClutch &&
                       rpm <= s_state.idleRPM + 0.012f &&
                       usefulSpeed < idleRoadSpeed &&
                       s_state.torqueReserve < 0.0f;
  if (bogging) {
    const float clutchLoad =
        Clamp01((engagement - stallClutch) / (1.0f - stallClutch));
    const float torqueDeficit = Clamp01(-s_state.torqueReserve);
    s_state.stallProgress +=
        clutchLoad * speedGap * torqueDeficit * dt *
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
            float throttle, float speedMps, bool engineOn) {
  const float dt = std::clamp(MISC::GET_FRAME_TIME(), 0.001f, 0.05f);
  const float rpm = data.GetRPM();

  if (engineOn && throttle < 0.01f && rpm > 0.05f && rpm < 0.35f)
    s_state.idleRPM += (rpm - s_state.idleRPM) * Clamp01(dt * 2.0f);

  const float inertia = data.GetDriveInertia();
  s_state.handlingBacked =
      std::isfinite(inertia) && inertia >= 0.05f && inertia <= 10.0f;
  s_state.inertia = s_state.handlingBacked ? inertia : 1.0f;

  const bool open = gear == 0 || clutchDisengagement > 0.40f;
  UpdateFreeRev(data, open, Clamp01(throttle), engineOn, dt);

  if (!open && engineOn) {
    data.SetThrottle(Clamp01(throttle));
    data.SetThrottlePedal(Clamp01(throttle));

    const uint8_t ratioIndex =
        gear < 0 ? 0 : static_cast<uint8_t>(gear);
    const float connectedRatio = std::fabs(data.GetGearRatio(ratioIndex));
    const float maxFlatVel = std::fabs(data.GetDriveMaxFlatVel());
    if (connectedRatio > 0.01f && maxFlatVel > 1.0f) {
      s_state.expectedRPM = std::clamp(
          std::max(s_state.idleRPM,
                   std::fabs(speedMps) * connectedRatio / maxFlatVel),
          s_state.idleRPM, 1.0f);
      const bool shouldSync =
          clutchEngagement > 0.80f &&
          (std::fabs(speedMps) > 1.5f || throttle < 0.10f);
      if (shouldSync) {
        const float syncRate =
            std::clamp(Config::ConnectedRPMSync, 0.0f, 1.0f);
        const float corrected =
            data.GetRPM() + (s_state.expectedRPM - data.GetRPM()) *
                                Clamp01(syncRate * dt * 8.0f);
        data.SetRPM(std::clamp(corrected, s_state.idleRPM, 1.0f));
      }
    }
  } else {
    s_state.expectedRPM = data.GetRPM();
  }

  float ratio = gear > 0 ? data.GetGearRatio(static_cast<uint8_t>(gear)) : 0.0f;
  float top = maxGear > 0
                  ? data.GetGearRatio(static_cast<uint8_t>(maxGear))
                  : 0.0f;
  if (engineOn && gear > 0 && clutchEngagement > 0.6f &&
      throttle < 0.05f && ratio > 0.0f && top > 0.0f) {
    const float target =
        Clamp01((ratio / top) * std::fabs(speedMps) / 80.0f);
    s_state.engineBrake +=
        (target - s_state.engineBrake) * Clamp01(dt * 8.0f);
  } else {
    s_state.engineBrake +=
        (0.0f - s_state.engineBrake) * Clamp01(dt * 8.0f);
  }

  const bool stalled = UpdateLoadAndStall(
      data, gear, clutchEngagement, throttle, speedMps, engineOn, dt);
  if (stalled)
    VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
  return stalled;
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
