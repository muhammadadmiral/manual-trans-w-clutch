#include "LaunchControl.h"
#include "../VehicleData.h"
#include "../../Core/Config.h"
#include <algorithm>
#include <cmath>

namespace LaunchControl {

static State s_state;

void Reset() {
  s_state = State{};
}

void Update(VehicleData &data, int gear, float clutchDisengagement,
            float throttle, float speedMps, bool engineOn) {
  s_state.targetRPM = std::clamp(Config::LaunchControlRPM, 0.40f, 0.95f);
  s_state.active = Config::LaunchControl && engineOn && gear == 1 &&
                   clutchDisengagement > 0.80f &&
                   throttle > 0.90f && std::fabs(speedMps) < 1.0f;
  if (s_state.active && data.GetRPM() > s_state.targetRPM) {
    data.SetRPM(s_state.targetRPM);
    data.SetThrottle(0.0f);
  }
}

bool IsActive() { return s_state.active; }
const State &GetState() { return s_state; }

} // namespace LaunchControl
