#include "ClutchSystem.h"
#include "../VehicleData.h"
#include "../../Core/Config.h"
#include "../../../sdk/inc/natives.h"
#include <algorithm>
#include <cmath>

namespace ClutchSystem {

static State s_state;

static float Clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

static float SmoothStep(float value) {
  value = Clamp01(value);
  return value * value * (3.0f - 2.0f * value);
}

void Reset() {
  s_state = State{};
}

float UpdatePedal(float rawPedal, float throttle, bool engineOn) {
  const float freePlayEnd =
      std::clamp(Config::ClutchBiteStart, 0.02f, 0.80f);
  const float fullyOpenAt =
      std::clamp(Config::ClutchBiteEnd, freePlayEnd + 0.05f, 0.98f);

  const float travel =
      (Clamp01(rawPedal) - freePlayEnd) / (fullyOpenAt - freePlayEnd);
  float disengagement = SmoothStep(travel);
  float engagement = 1.0f - disengagement;

  const float dt = std::clamp(MISC::GET_FRAME_TIME(), 0.0f, 0.05f);
  s_state.slipping = engineOn && engagement > 0.01f && engagement < 0.99f;
  if (s_state.slipping) {
    const float slipEnergy =
        (1.0f - engagement) * (0.25f + Clamp01(throttle) * 0.75f);
    s_state.heat += slipEnergy * std::max(0.0f, Config::ClutchHeatRate) * dt;
  } else {
    s_state.heat -= std::max(0.0f, Config::ClutchCoolRate) * dt;
  }
  s_state.heat = Clamp01(s_state.heat);

  const float fadeStart =
      std::clamp(Config::ClutchFadeStart, 0.50f, 0.99f);
  if (s_state.heat > fadeStart) {
    const float fade = (s_state.heat - fadeStart) / (1.0f - fadeStart);
    engagement *=
        1.0f - Clamp01(Config::ClutchFadeStrength) * Clamp01(fade);
    disengagement = 1.0f - engagement;
  }

  s_state.disengagement = disengagement;
  s_state.engagement = engagement;
  return disengagement;
}

void ApplyToVehicle(VehicleData &data, int gear, float speedMps) {
  const bool fullyOpen = s_state.disengagement >= 0.995f;

  if (gear == 0 || fullyOpen) {
    s_state.nativeActuator = std::fabs(speedMps) < 1.0f ? -5.0f : -0.5f;
  } else if (gear > 1 && s_state.disengagement > 0.001f) {
    // Di gear tinggi GTA baru nangkep slip mulai sekitar 0.6.
    s_state.nativeActuator = 0.6f + s_state.engagement * 0.4f;
  } else {
    s_state.nativeActuator = s_state.engagement;
  }

  data.SetClutch(s_state.nativeActuator);
}

float GetEngagement() { return s_state.engagement; }
float GetHeat() { return s_state.heat; }
float GetNativeActuator() { return s_state.nativeActuator; }
bool IsDrivelineOpen(int gear) {
  return gear == 0 || s_state.disengagement > 0.40f;
}
const State &GetState() { return s_state; }

} // namespace ClutchSystem
