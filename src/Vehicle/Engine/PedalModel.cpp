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

void Reset() { s_state = State{}; }

void Update(float rawThrottle, float rawBrake, float clutchDisengagement,
            int gear, float signedSpeedMps, bool automaticMode,
            bool engineOn) {
  const float dt = std::clamp(MISC::GET_FRAME_TIME(), 0.001f, 0.05f);
  s_state.throttle = engineOn ? Clamp01(rawThrottle) : 0.0f;
  s_state.brake = Clamp01(rawBrake);
  s_state.heelToeWindow =
      !automaticMode && clutchDisengagement > 0.35f && gear != 0;

  const bool pedalsOverlap =
      s_state.throttle > 0.10f && s_state.brake > 0.20f;
  if (pedalsOverlap)
    s_state.overlapTime += dt;
  else
    s_state.overlapTime =
        (std::max)(0.0f, s_state.overlapTime - dt * 4.0f);

  const bool launchWindow =
      Config::LaunchControl && std::fabs(signedSpeedMps) < 1.0f &&
      gear > 0 && s_state.throttle > 0.50f;
  const bool shouldOverride =
      Config::BrakeThrottleOverride && pedalsOverlap &&
      !s_state.heelToeWindow && !launchWindow &&
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
