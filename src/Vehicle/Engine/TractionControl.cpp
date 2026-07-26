#include "TractionControl.h"
#include "../VehicleData.h"
#include "../../Core/Config.h"
#include "../../../sdk/inc/natives.h"
#include <algorithm>
#include <cmath>

namespace TractionControl {

static State s_state;

static float Clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

void Reset() {
  const bool enabled = s_state.enabled;
  s_state = State{};
  s_state.enabled = enabled;
}

void Update(Vehicle vehicle, VehicleData &data, float speedMps,
            int gear, float clutchDisengagement, float &throttle) {
  s_state.enabled = Config::TcsEnabled;
  const float dt = std::clamp(MISC::GET_FRAME_TIME(), 0.001f, 0.05f);
  const uint8_t count = data.GetWheelCount();
  float fastestDrivenOmega = 0.0f;
  float allOmega = 0.0f;
  int valid = 0;
  int driven = 0;

  for (uint8_t i = 0; i < count; ++i) {
    const auto wheel = data.GetWheelTelemetry(i);
    if (!wheel.valid || !std::isfinite(wheel.angularVelocity))
      continue;
    const float omega = std::fabs(wheel.angularVelocity);
    allOmega += omega;
    ++valid;

    // Power field nol di roda bebas. Pas coast fallback ke rata-rata semua.
    const float powerWeight = std::fabs(wheel.power);
    if (powerWeight > 0.001f) {
      fastestDrivenOmega = std::max(fastestDrivenOmega, omega);
      ++driven;
    }
  }

  s_state.validWheelCount = valid;
  s_state.drivenWheelCount = driven;
  s_state.wheelDataValid = valid >= 2;
  if (!s_state.wheelDataValid) {
    s_state.active = false;
    s_state.cutLevel +=
        (0.0f - s_state.cutLevel) * Clamp01(dt * 8.0f);
    s_state.slipRatio = 0.0f;
    s_state.rawSlipRatio = 0.0f;
    return;
  }

  const float omega =
      driven > 0 ? fastestDrivenOmega
                 : allOmega / static_cast<float>(valid);
  const float roadSpeed = std::fabs(speedMps);
  if (throttle < 0.05f && roadSpeed > 3.0f && omega > 1.0f) {
    const float measuredRadius = roadSpeed / omega;
    if (measuredRadius > 0.15f && measuredRadius < 0.80f)
      s_state.rollingRadius +=
          (measuredRadius - s_state.rollingRadius) * 0.03f;
  }

  const float wheelSpeed = omega * s_state.rollingRadius;
  s_state.rawSlipRatio =
      (wheelSpeed - roadSpeed) / std::max(roadSpeed, 3.0f);
  const float slipAlpha = 1.0f - std::exp(-14.0f * dt);
  s_state.slipRatio +=
      (s_state.rawSlipRatio - s_state.slipRatio) * slipAlpha;

  const float target = std::clamp(Config::TcsSlipTarget, 0.02f, 0.60f);
  const bool interventionAllowed =
      s_state.enabled && gear > 0 && clutchDisengagement < 0.40f &&
      throttle > 0.10f && roadSpeed > 1.0f;
  const float requestedCut =
      interventionAllowed && s_state.slipRatio > target
          ? Clamp01((s_state.slipRatio - target) /
                    std::max(0.10f, 0.50f - target))
          : 0.0f;
  const float cutRate = requestedCut > s_state.cutLevel ? 12.0f : 5.0f;
  s_state.cutLevel +=
      (requestedCut - s_state.cutLevel) * Clamp01(dt * cutRate);
  s_state.active = interventionAllowed && s_state.cutLevel > 0.01f;
  if (s_state.active) {
    throttle *=
        1.0f - Clamp01(Config::TcsMaxCut) * s_state.cutLevel;
    PAD::DISABLE_CONTROL_ACTION(0, 71, true);
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 71, throttle);
  } else {
    s_state.active = false;
  }

  (void)vehicle;
}

void ToggleTCS() {
  Config::TcsEnabled = !Config::TcsEnabled;
  s_state.enabled = Config::TcsEnabled;
}
bool IsTCSActive() { return s_state.active; }
const State &GetState() { return s_state; }

} // namespace TractionControl
