#include "TractionControl.h"
#include "../VehicleData.h"
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
  const uint8_t count = data.GetWheelCount();
  float drivenOmega = 0.0f;
  float drivenWeight = 0.0f;
  float allOmega = 0.0f;
  int valid = 0;

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
      drivenOmega += omega * powerWeight;
      drivenWeight += powerWeight;
    }
  }

  s_state.wheelDataValid = valid >= 2;
  if (!s_state.wheelDataValid) {
    s_state.active = false;
    s_state.cutLevel = 0.0f;
    s_state.slipRatio = 0.0f;
    return;
  }

  const float omega =
      drivenWeight > 0.001f ? drivenOmega / drivenWeight
                            : allOmega / static_cast<float>(valid);
  const float roadSpeed = std::fabs(speedMps);
  if (throttle < 0.05f && roadSpeed > 3.0f && omega > 1.0f) {
    const float measuredRadius = roadSpeed / omega;
    if (measuredRadius > 0.15f && measuredRadius < 0.80f)
      s_state.rollingRadius +=
          (measuredRadius - s_state.rollingRadius) * 0.03f;
  }

  const float wheelSpeed = omega * s_state.rollingRadius;
  s_state.slipRatio =
      (wheelSpeed - roadSpeed) / std::max(roadSpeed, 3.0f);

  if (s_state.enabled && gear > 0 && clutchDisengagement < 0.40f &&
      throttle > 0.10f && roadSpeed > 3.0f && s_state.slipRatio > 0.12f) {
    s_state.cutLevel = Clamp01((s_state.slipRatio - 0.12f) / 0.35f);
    s_state.active = true;
    throttle *= 1.0f - 0.75f * s_state.cutLevel;
    PAD::DISABLE_CONTROL_ACTION(0, 71, true);
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 71, throttle);
  } else {
    s_state.active = false;
    s_state.cutLevel = 0.0f;
  }

  (void)vehicle;
}

void ToggleTCS() { s_state.enabled = !s_state.enabled; }
bool IsTCSActive() { return s_state.active; }
const State &GetState() { return s_state; }

} // namespace TractionControl
