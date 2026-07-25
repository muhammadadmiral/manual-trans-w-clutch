#include "BrakeSystem.h"
#include "../VehicleData.h"
#include "../../../sdk/inc/natives.h"
#include <algorithm>
#include <cmath>

namespace BrakeSystem {

static State s_state;

static float Clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

void Reset() {
  const bool enabled = s_state.absEnabled;
  s_state = State{};
  s_state.absEnabled = enabled;
}

float UpdateABS(Vehicle vehicle, VehicleData &data, float brakeInput,
                float speedMps) {
  const uint8_t count = data.GetWheelCount();
  float weightedOmega = 0.0f;
  float totalLoad = 0.0f;
  int valid = 0;

  for (uint8_t i = 0; i < count; ++i) {
    const auto wheel = data.GetWheelTelemetry(i);
    if (!wheel.valid || !std::isfinite(wheel.angularVelocity))
      continue;
    const float load = wheel.load > 0.01f ? wheel.load : 1.0f;
    weightedOmega += std::fabs(wheel.angularVelocity) * load;
    totalLoad += load;
    ++valid;
  }

  s_state.wheelDataValid = valid >= 2 && totalLoad > 0.01f;
  if (!s_state.wheelDataValid) {
    s_state.absActive = false;
    s_state.absLevel = 0.0f;
    s_state.wheelSlip = 0.0f;
    return Clamp01(brakeInput);
  }

  const float omega = weightedOmega / totalLoad;
  const float roadSpeed = std::fabs(speedMps);
  if (brakeInput < 0.05f && roadSpeed > 3.0f && omega > 1.0f) {
    const float measuredRadius = roadSpeed / omega;
    if (measuredRadius > 0.15f && measuredRadius < 0.80f)
      s_state.rollingRadius +=
          (measuredRadius - s_state.rollingRadius) * 0.03f;
  }

  const float wheelSpeed = omega * s_state.rollingRadius;
  s_state.wheelSlip =
      roadSpeed > 1.0f ? Clamp01((roadSpeed - wheelSpeed) / roadSpeed) : 0.0f;

  if (s_state.absEnabled && brakeInput > 0.25f && roadSpeed > 3.0f &&
      s_state.wheelSlip > 0.16f) {
    s_state.absLevel = Clamp01((s_state.wheelSlip - 0.16f) / 0.24f);
    s_state.absActive = true;
    const float pressure = brakeInput * (1.0f - 0.70f * s_state.absLevel);
    PAD::DISABLE_CONTROL_ACTION(0, 72, true);
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 72, pressure);
    return pressure;
  }

  s_state.absActive = false;
  s_state.absLevel = 0.0f;
  (void)vehicle;
  return Clamp01(brakeInput);
}

void ToggleABS() { s_state.absEnabled = !s_state.absEnabled; }
bool IsABSActive() { return s_state.absActive; }
const State &GetState() { return s_state; }

} // namespace BrakeSystem
