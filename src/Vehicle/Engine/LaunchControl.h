#pragma once

class VehicleData;

namespace LaunchControl {

struct State {
  bool enabled = false;
  bool armed = false;
  bool active = false;
  bool limiting = false;
  float targetRPM = 0.72f;
};

void Reset();
void Update(VehicleData &data, int gear, float clutchDisengagement,
            float throttle, float brake, float speedMps, bool engineOn,
            bool automaticMode = false);
bool IsActive();
const State &GetState();

} // namespace LaunchControl
