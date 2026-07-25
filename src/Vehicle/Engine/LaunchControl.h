#pragma once

class VehicleData;

namespace LaunchControl {

struct State {
  bool active = false;
  float targetRPM = 0.72f;
};

void Reset();
void Update(VehicleData &data, int gear, float clutchDisengagement,
            float throttle, float speedMps, bool engineOn);
bool IsActive();
const State &GetState();

} // namespace LaunchControl
