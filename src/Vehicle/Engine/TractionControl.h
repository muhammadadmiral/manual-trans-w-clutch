#pragma once

class VehicleData;
using Vehicle = int;

namespace TractionControl {

struct State {
  bool enabled = true;
  bool active = false;
  bool wheelDataValid = false;
  float slipRatio = 0.0f;
  float cutLevel = 0.0f;
  float rollingRadius = 0.34f;
};

void Reset();
void Update(Vehicle vehicle, VehicleData &data, float speedMps,
            int gear, float clutchDisengagement, float &throttle);
void ToggleTCS();
bool IsTCSActive();
const State &GetState();

} // namespace TractionControl
