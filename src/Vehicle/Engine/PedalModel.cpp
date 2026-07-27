#include "PedalModel.h"
#include "../../../sdk/inc/natives.h"
#include <algorithm>

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
  const float throttleTarget = engineOn ? Clamp01(rawThrottle) : 0.0f;
  const float brakeTarget = Clamp01(rawBrake);
  s_state.automaticActuator = automaticMode;
  // PAD already supplies the vehicle's native analog pedal values. A second
  // attack/release/expo controller made pedal output chase the native value
  // and caused the ETC-like RPM rebound reported during shifts.
  s_state.actuatorThrottle = throttleTarget;
  s_state.actuatorBrake = brakeTarget;
  s_state.throttle = throttleTarget;
  s_state.brake = brakeTarget;
  s_state.heelToeWindow =
      !automaticMode && clutchDisengagement > 0.35f && gear != 0;

  const bool pedalsOverlap =
      s_state.throttle > 0.10f && s_state.brake > 0.20f;
  if (pedalsOverlap)
    s_state.overlapTime += dt;
  else
    s_state.overlapTime =
        (std::max)(0.0f, s_state.overlapTime - dt * 4.0f);

  // No electronic throttle override: simultaneous brake/throttle remains a
  // driver command (heel-toe, left-foot braking, burnout). TCS/ESC are
  // separate, explicit assists and can still intervene when enabled.
  s_state.brakeOverrideActive = false;
  (void)signedSpeedMps;
}

float GetThrottle() { return s_state.throttle; }
float GetBrake() { return s_state.brake; }
bool IsBrakeOverrideActive() { return s_state.brakeOverrideActive; }
bool IsHeelToeWindow() { return s_state.heelToeWindow; }
const State &GetState() { return s_state; }

} // namespace PedalModel
