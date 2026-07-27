#include "LaunchControl.h"
#include "../VehicleData.h"
#include "../Maintenance/WorkshopTuning.h"
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
            float &throttle, float brake, float speedMps, bool engineOn,
            bool automaticMode) {
  s_state.targetRPM = std::clamp(Config::LaunchControlRPM, 0.40f, 0.95f);
  s_state.targetRPM = std::clamp(
      s_state.targetRPM + WorkshopTuning::GetLaunchTargetOffset(),
      0.36f, 0.98f);
  s_state.enabled = Config::LaunchControl;
  const bool drivelineHeld =
      automaticMode ? brake > 0.70f : clutchDisengagement > 0.80f;
  s_state.armed = s_state.enabled && engineOn && gear == 1 &&
                  drivelineHeld && throttle > 0.72f &&
                  std::fabs(speedMps) < 2.0f;
  s_state.active = s_state.armed;
  s_state.limiting =
      s_state.armed && data.GetRPM() > s_state.targetRPM;
  const float dt = std::clamp(MISC::GET_FRAME_TIME(), 0.001f, 0.05f);
  const float rpmError =
      s_state.armed
          ? std::max(0.0f, data.GetRPM() - s_state.targetRPM)
          : 0.0f;
  const float requestedCut =
      s_state.limiting
          ? std::clamp(rpmError / 0.12f, 0.0f, 1.0f)
          : 0.0f;
  const float response =
      requestedCut > s_state.cutLevel ? 22.0f : 9.0f;
  s_state.cutLevel +=
      (requestedCut - s_state.cutLevel) *
      std::clamp(dt * response, 0.0f, 1.0f);
  s_state.outputThrottle =
      std::clamp(
          throttle *
              (1.0f - WorkshopTuning::GetLaunchCutAggression() *
                          s_state.cutLevel),
          0.0f, 1.0f);
  if (s_state.limiting) {
    // Progressive soft cut lewat pedal GTA; RPM/redline native tidak ditulis.
    PAD::DISABLE_CONTROL_ACTION(0, 71, true);
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 71, s_state.outputThrottle);
    throttle = s_state.outputThrottle;
  }
}

bool IsActive() { return s_state.active; }
const State &GetState() { return s_state; }

} // namespace LaunchControl
