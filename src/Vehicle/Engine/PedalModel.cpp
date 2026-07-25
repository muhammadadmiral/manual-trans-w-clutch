#include "PedalModel.h"
#include "../../Core/Config.h"
#include "../../../sdk/inc/natives.h"
#include <algorithm>
#include <cmath>

namespace PedalModel {

static State s_state;

static float Clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

static float Follow(float current, float target, float attack, float release,
                    float dt) {
  const float tau = target > current ? attack : release;
  const float alpha =
      tau > 0.001f ? 1.0f - std::exp(-dt / tau) : 1.0f;
  return current + (target - current) * Clamp01(alpha);
}

void Reset() { s_state = State{}; }

void Update(float rawThrottle, float rawBrake, float clutchDisengagement,
            int gear, float signedSpeedMps, bool automaticMode,
            bool engineOn) {
  const float dt = std::clamp(MISC::GET_FRAME_TIME(), 0.001f, 0.05f);
  const float throttleTarget = engineOn ? Clamp01(rawThrottle) : 0.0f;
  const float brakeTarget = Clamp01(rawBrake);
  s_state.automaticActuator = automaticMode;
  if (automaticMode) {
    s_state.actuatorThrottle =
        Follow(s_state.actuatorThrottle, throttleTarget,
               std::clamp(Config::AutomaticThrottleAttack, 0.01f, 1.50f),
               std::clamp(Config::AutomaticThrottleRelease, 0.01f, 1.50f),
               dt);
    s_state.actuatorBrake =
        Follow(s_state.actuatorBrake, brakeTarget,
               std::clamp(Config::AutomaticBrakeAttack, 0.01f, 1.50f),
               std::clamp(Config::AutomaticBrakeRelease, 0.01f, 1.50f),
               dt);
  } else {
    s_state.actuatorThrottle = throttleTarget;
    s_state.actuatorBrake = brakeTarget;
  }
  s_state.throttle = Clamp01(s_state.actuatorThrottle);
  s_state.brake = Clamp01(s_state.actuatorBrake);
  s_state.heelToeWindow =
      !automaticMode && clutchDisengagement > 0.35f && gear != 0;

  const bool pedalsOverlap =
      s_state.throttle > 0.10f && s_state.brake > 0.20f;
  if (pedalsOverlap)
    s_state.overlapTime += dt;
  else
    s_state.overlapTime =
        (std::max)(0.0f, s_state.overlapTime - dt * 4.0f);

  const bool powerBrakeWindow =
      !automaticMode && std::fabs(signedSpeedMps) < 2.0f &&
      gear == 1 && s_state.throttle > 0.50f;
  const bool launchWindow =
      Config::LaunchControl && std::fabs(signedSpeedMps) < 1.0f &&
      gear > 0 && s_state.throttle > 0.50f;
  const bool shouldOverride =
      Config::BrakeThrottleOverride && pedalsOverlap &&
      !s_state.heelToeWindow && !powerBrakeWindow && !launchWindow &&
      s_state.overlapTime >=
          (std::max)(0.0f, Config::BrakeOverrideDelay);

  s_state.brakeOverrideActive = shouldOverride;
  if (shouldOverride) {
    const float cut =
        Clamp01(Config::BrakeOverrideCut) * Clamp01(s_state.brake);
    s_state.throttle *= 1.0f - cut;
  }
}

float GetThrottle() { return s_state.throttle; }
float GetBrake() { return s_state.brake; }
bool IsBrakeOverrideActive() { return s_state.brakeOverrideActive; }
bool IsHeelToeWindow() { return s_state.heelToeWindow; }
const State &GetState() { return s_state; }

} // namespace PedalModel
