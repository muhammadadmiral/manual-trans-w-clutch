#include "BrakeSystem.h"
#include "../VehicleData.h"
#include "../Maintenance/WorkshopTuning.h"
#include "../../Core/Config.h"
#include "../../../sdk/inc/natives.h"
#include <algorithm>
#include <array>
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
  std::array<GameMemory::WheelTelemetry, 16> wheels{};
  float weightedOmega = 0.0f;
  float totalLoad = 0.0f;
  float slowestOmega = 1000000.0f;
  float maximumLoad = 0.0f;
  int valid = 0;

  for (uint8_t i = 0; i < count; ++i) {
    const auto wheel = data.GetWheelTelemetry(i);
    if (!wheel.valid || !std::isfinite(wheel.angularVelocity))
      continue;
    wheels[i] = wheel;
    maximumLoad = std::max(maximumLoad, wheel.load);
    ++valid;
  }
  for (uint8_t i = 0; i < count; ++i) {
    const auto &wheel = wheels[i];
    if (!wheel.valid || !std::isfinite(wheel.angularVelocity))
      continue;
    const float load = maximumLoad > 0.01f
                           ? std::max(0.0f, wheel.load)
                           : 1.0f;
    weightedOmega += std::fabs(wheel.angularVelocity) * load;
    totalLoad += load;
    // Ignore a nearly airborne wheel; real ABS also deprioritizes a sensor
    // whose normal load has collapsed during pitch/curb contact.
    if (maximumLoad <= 0.01f || load >= maximumLoad * 0.14f)
      slowestOmega =
          std::min(slowestOmega, std::fabs(wheel.angularVelocity));
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
  s_state.slipRate =
      (s_state.wheelSlip - s_state.previousWheelSlip) / dt;
  s_state.previousWheelSlip = s_state.wheelSlip;

  const float target = std::clamp(
      Config::AbsSlipTarget *
          WorkshopTuning::GetAbsSlipMultiplier(),
      0.04f, 0.75f);
  const bool interventionAllowed =
      s_state.absEnabled && effectiveBrake > 0.20f && roadSpeed > 2.0f;
  const float proportionalRelease =
      interventionAllowed && s_state.wheelSlip > target
          ? Clamp01((s_state.wheelSlip - target) /
                    std::max(0.10f, 0.50f - target))
          : 0.0f;
  const float predictiveRelease =
      interventionAllowed && s_state.slipRate > 0.40f
          ? Clamp01((s_state.slipRate - 0.40f) / 3.0f) * 0.28f
          : 0.0f;
  const float requestedRelease =
      std::max(proportionalRelease, predictiveRelease);
  const float releaseRate =
      requestedRelease > s_state.absLevel ? 20.0f : 12.0f;
  s_state.absLevel +=
      (requestedRelease - s_state.absLevel) * Clamp01(dt * releaseRate);
  s_state.absActive = interventionAllowed && s_state.absLevel > 0.01f;
  if (s_state.absActive) {
    s_state.pulsePhase =
        std::fmod(s_state.pulsePhase + dt * 34.0f, 6.2831853f);
    const float pulse =
        0.92f + std::sin(s_state.pulsePhase) * 0.08f;
    const float pressure =
        effectiveBrake *
        (1.0f - Clamp01(
                    Config::AbsMaxRelease *
                    WorkshopTuning::GetAbsReleaseMultiplier()) *
                    s_state.absLevel) *
        pulse;
    s_state.outputPressure = Clamp01(pressure);
    const int brakeControl = reverse ? 71 : 72;
    PAD::DISABLE_CONTROL_ACTION(0, brakeControl, true);
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(
        0, brakeControl, s_state.outputPressure);
    return s_state.outputPressure;
  }

  s_state.absActive = false;
  s_state.outputPressure = effectiveBrake;
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
