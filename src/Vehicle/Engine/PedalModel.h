#pragma once

namespace PedalModel {

struct State {
  float throttle = 0.0f;
  float brake = 0.0f;
  float overlapTime = 0.0f;
  bool brakeOverrideActive = false;
  bool heelToeWindow = false;
};

void Reset();
void Update(float rawThrottle, float rawBrake, float clutchDisengagement,
            int gear, float signedSpeedMps, bool automaticMode,
            bool engineOn);
float GetThrottle();
float GetBrake();
bool IsBrakeOverrideActive();
bool IsHeelToeWindow();
const State &GetState();

} // namespace PedalModel
