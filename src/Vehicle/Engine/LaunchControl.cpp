#include "LaunchControl.h"
#include "../VehicleData.h"
#include "../../Core/Config.h"
#include "../../../sdk/inc/natives.h"
#include <algorithm>
#include <cmath>

namespace LaunchControl {

static State s_state;

void Reset() {
  s_state = State{};
}

void Update(VehicleData &data, int gear, float clutchDisengagement,
            float throttle, float brake, float speedMps, bool engineOn,
            bool automaticMode) {
  s_state.targetRPM = std::clamp(Config::LaunchControlRPM, 0.40f, 0.95f);
  s_state.enabled = Config::LaunchControl;
  const bool drivelineHeld =
      automaticMode ? brake > 0.70f : clutchDisengagement > 0.80f;
  s_state.armed = s_state.enabled && engineOn && gear == 1 &&
                  drivelineHeld && throttle > 0.90f &&
                  std::fabs(speedMps) < 1.0f;
  s_state.active = s_state.armed;
  s_state.limiting =
      s_state.armed && data.GetRPM() > s_state.targetRPM;
  if (s_state.limiting) {
    // Soft cut lewat pedal GTA; jangan menulis RPM mesin.
    PAD::DISABLE_CONTROL_ACTION(0, 71, true);
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 71, 0.0f);
  }
}

bool IsActive() { return s_state.active; }
const State &GetState() { return s_state; }

} // namespace LaunchControl
