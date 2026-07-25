#include "BrakeSystem.h"
#include "../VehicleData.h"
#include "../../Core/Config.h"
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
                float speedMps, bool reverse) {
  s_state.absEnabled = Config::AbsEnabled;
  const float dt = std::clamp(MISC::GET_FRAME_TIME(), 0.001f, 0.05f);
  const float roadSpeed = std::fabs(speedMps);
  const float heatLoad =
      Clamp01(brakeInput) * (1.0f + std::min(3.0f, roadSpeed / 15.0f));
  if (brakeInput > 0.05f && roadSpeed > 1.0f) {
    s_state.temperature +=
        heatLoad * std::max(0.0f, Config::BrakeHeatRate) * dt;
  } else {
    s_state.temperature -=
        std::max(0.0f, Config::BrakeCoolRate) * dt;
  }
  s_state.temperature = Clamp01(s_state.temperature);
  const float fadeStart =
      std::clamp(Config::BrakeFadeStart, 0.40f, 0.99f);
  s_state.fadeLevel =
      Config::BrakeFadeEnabled && s_state.temperature > fadeStart
          ? Clamp01((s_state.temperature - fadeStart) /
                    (1.0f - fadeStart)) *
                Clamp01(Config::BrakeFadeStrength)
          : 0.0f;
  const float effectiveBrake =
      Clamp01(brakeInput) * (1.0f - s_state.fadeLevel);

  const uint8_t count = data.GetWheelCount();
  float weightedOmega = 0.0f;
  float totalLoad = 0.0f;
  float slowestOmega = 1000000.0f;
  int valid = 0;

  for (uint8_t i = 0; i < count; ++i) {
    const auto wheel = data.GetWheelTelemetry(i);
    if (!wheel.valid || !std::isfinite(wheel.angularVelocity))
      continue;
    const float load = wheel.load > 0.01f ? wheel.load : 1.0f;
    weightedOmega += std::fabs(wheel.angularVelocity) * load;
    slowestOmega = std::min(slowestOmega, std::fabs(wheel.angularVelocity));
    totalLoad += load;
    ++valid;
  }

  s_state.validWheelCount = valid;
  s_state.wheelDataValid = valid >= 2 && totalLoad > 0.01f;
  if (!s_state.wheelDataValid) {
    s_state.absActive = false;
    s_state.absLevel +=
        (0.0f - s_state.absLevel) * Clamp01(dt * 10.0f);
    s_state.wheelSlip = 0.0f;
    s_state.rawWheelSlip = 0.0f;
    return effectiveBrake;
  }

  const float omega = weightedOmega / totalLoad;
  if (brakeInput < 0.05f && roadSpeed > 3.0f && omega > 1.0f) {
    const float measuredRadius = roadSpeed / omega;
    if (measuredRadius > 0.15f && measuredRadius < 0.80f)
      s_state.rollingRadius +=
          (measuredRadius - s_state.rollingRadius) * 0.03f;
  }

  const float slowestWheelSpeed = slowestOmega * s_state.rollingRadius;
  s_state.rawWheelSlip =
      roadSpeed > 1.0f
          ? Clamp01((roadSpeed - slowestWheelSpeed) / roadSpeed)
          : 0.0f;
  const float slipAlpha = 1.0f - std::exp(-18.0f * dt);
  s_state.wheelSlip +=
      (s_state.rawWheelSlip - s_state.wheelSlip) * slipAlpha;

  const float target = std::clamp(Config::AbsSlipTarget, 0.05f, 0.60f);
  const bool interventionAllowed =
      s_state.absEnabled && effectiveBrake > 0.20f && roadSpeed > 2.0f;
  const float requestedRelease =
      interventionAllowed && s_state.wheelSlip > target
          ? Clamp01((s_state.wheelSlip - target) /
                    std::max(0.10f, 0.50f - target))
          : 0.0f;
  const float releaseRate =
      requestedRelease > s_state.absLevel ? 20.0f : 12.0f;
  s_state.absLevel +=
      (requestedRelease - s_state.absLevel) * Clamp01(dt * releaseRate);
  s_state.absActive = interventionAllowed && s_state.absLevel > 0.01f;
  if (s_state.absActive) {
    const float pressure =
        effectiveBrake *
        (1.0f - Clamp01(Config::AbsMaxRelease) * s_state.absLevel);
    const int brakeControl = reverse ? 71 : 72;
    PAD::DISABLE_CONTROL_ACTION(0, brakeControl, true);
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, brakeControl, pressure);
    return pressure;
  }

  s_state.absActive = false;
  (void)vehicle;
  return effectiveBrake;
}

void ToggleABS() {
  Config::AbsEnabled = !Config::AbsEnabled;
  s_state.absEnabled = Config::AbsEnabled;
}
bool IsABSActive() { return s_state.absActive; }
float GetTemperature() { return s_state.temperature; }
float GetFadeLevel() { return s_state.fadeLevel; }
const State &GetState() { return s_state; }

} // namespace BrakeSystem
