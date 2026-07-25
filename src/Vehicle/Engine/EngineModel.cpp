#include "EngineModel.h"
#include "../VehicleData.h"
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
  const float speedGap =
      idleRoadSpeed > 0.01f
          ? Clamp01((idleRoadSpeed - std::fabs(speedMps)) / idleRoadSpeed)
          : 0.0f;
  s_state.load = engagement * speedGap;

  const float rpm = data.GetRPM();
  const bool bogging = engagement > 0.65f &&
                       rpm <= s_state.idleRPM + 0.006f &&
                       std::fabs(speedMps) < idleRoadSpeed;
  if (bogging) {
    const float clutchLoad = Clamp01((engagement - 0.65f) / 0.35f);
    s_state.stallProgress += clutchLoad * speedGap * dt * 2.0f;
  } else {
    s_state.stallProgress =
        std::max(0.0f, s_state.stallProgress - dt * 3.0f);
  }

  // Throttle gak dipakai sebagai shortcut. Kalau torsinya cukup, RPM native
  // bakal keluar dari idle dan progress otomatis batal.
  (void)throttle;
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
const State &GetState() { return s_state; }

} // namespace EngineModel
