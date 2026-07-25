#include "GearboxSystem.h"
#include "../VehicleData.h"
#include "../../../sdk/inc/natives.h"
#include <algorithm>
#include <cmath>

namespace GearboxSystem {

static State s_state;

void Reset() {
  s_state = State{};
}

void Update(VehicleData &data, int gear, int maxGear,
            float clutchDisengagement, float throttle, bool engineOn) {
  const float dt = std::clamp(MISC::GET_FRAME_TIME(), 0.001f, 0.05f);
  const float rpm = data.GetRPM();

  if (engineOn && gear > 0 && gear < maxGear &&
      clutchDisengagement < 0.20f && throttle > 0.95f && rpm > 0.985f) {
    s_state.overRev += dt;
    if (s_state.overRev > 1.5f) {
      s_state.health = std::max(0.0f, s_state.health - 0.001f);
      s_state.overRev = 0.0f;
    }
  } else {
    s_state.overRev = std::max(0.0f, s_state.overRev - dt * 2.0f);
  }
}

void NotifyGrind() {
  s_state.health = std::max(0.0f, s_state.health - 0.04f);
}

void NotifyRevMatch(float currentRPM, float targetRPM) {
  s_state.revMatchTarget = targetRPM;
  s_state.revMatched = std::fabs(currentRPM - targetRPM) < 0.15f;
}

float GetHealth() { return s_state.health; }
bool IsSeized() { return s_state.health <= 0.0f; }
const State &GetState() { return s_state; }

} // namespace GearboxSystem
