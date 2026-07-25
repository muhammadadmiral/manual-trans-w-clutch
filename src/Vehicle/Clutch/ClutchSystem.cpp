#include "ClutchSystem.h"
#include "../VehicleData.h"
#include "../../Core/Config.h"
#include "../../../sdk/inc/natives.h"
#include <algorithm>
#include <cmath>

namespace ClutchSystem {

static State s_state;

static float Clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

static float SmoothStep(float value) {
  value = Clamp01(value);
  return value * value * (3.0f - 2.0f * value);
}

void Reset() {
  s_state = State{};
}

float UpdatePedal(float rawPedal, float throttle, bool engineOn) {
  const float freePlayEnd =
      std::clamp(Config::ClutchBiteStart, 0.02f, 0.80f);
  const float fullyOpenAt =
      std::clamp(Config::ClutchBiteEnd, freePlayEnd + 0.05f, 0.98f);

  const float travel =
      (Clamp01(rawPedal) - freePlayEnd) / (fullyOpenAt - freePlayEnd);
  float disengagement = SmoothStep(travel);
  float engagement = 1.0f - disengagement;

  const float dt = std::clamp(MISC::GET_FRAME_TIME(), 0.0f, 0.05f);
  if (dt > 0.0001f) {
    s_state.releaseRate =
        (s_state.previousDisengagement - disengagement) / dt;
  } else {
    s_state.releaseRate = 0.0f;
  }
  const float dumpRate = std::max(1.0f, Config::ClutchDumpRate);
  const float freshDump =
      Clamp01((s_state.releaseRate - dumpRate) / dumpRate) *
      Clamp01(throttle) * engagement;
  if (engineOn && freshDump > 0.05f) {
    s_state.dumpSeverity =
        std::max(s_state.dumpSeverity, freshDump);
    s_state.dumpRemaining = 0.16f;
  } else if (s_state.dumpRemaining > 0.0f) {
    s_state.dumpRemaining =
        std::max(0.0f, s_state.dumpRemaining - dt);
    s_state.dumpSeverity *= std::max(0.0f, 1.0f - dt * 5.0f);
  } else {
    s_state.dumpSeverity = 0.0f;
  }
  s_state.previousDisengagement = disengagement;

  if (s_state.dumpRemaining > 0.0f &&
      Config::ClutchDumpShock > 0.01f) {
    PAD::SET_CONTROL_SHAKE(
        0, 100,
        static_cast<int>(80.0f + s_state.dumpSeverity *
                                     Clamp01(Config::ClutchDumpShock) *
                                     175.0f));
  }

  s_state.slipping = engineOn && engagement > 0.01f && engagement < 0.99f;
  if (s_state.slipping) {
    const float slipEnergy =
        (1.0f - engagement) * (0.25f + Clamp01(throttle) * 0.75f);
    s_state.heat += slipEnergy * std::max(0.0f, Config::ClutchHeatRate) * dt;
  } else {
    s_state.heat -= std::max(0.0f, Config::ClutchCoolRate) * dt;
  }
  s_state.heat = Clamp01(s_state.heat);

  const float fadeStart =
      std::clamp(Config::ClutchFadeStart, 0.50f, 0.99f);
  if (s_state.heat > fadeStart) {
    const float fade = (s_state.heat - fadeStart) / (1.0f - fadeStart);
    engagement *=
        1.0f - Clamp01(Config::ClutchFadeStrength) * Clamp01(fade);
    disengagement = 1.0f - engagement;
  }

  s_state.disengagement = disengagement;
  s_state.engagement = engagement;
  return disengagement;
}

void ApplyToVehicle(VehicleData &data, int gear, float speedMps) {
  const bool drivelineOpen = s_state.disengagement >= 0.88f;

  if (gear == 0 || drivelineOpen) {
    s_state.nativeActuator = 0.0f;
  } else {
    s_state.nativeActuator = s_state.engagement;
  }

  // Jangan tulis field sekitar RPM sebelum offset actuator-nya terbukti.
  // Coupling roda diterapkan lewat native drivetrain di orchestrator.
  (void)data;
  (void)speedMps;
}

float GetEngagement() { return s_state.engagement; }
float GetHeat() { return s_state.heat; }
float GetNativeActuator() { return s_state.nativeActuator; }
float GetDumpSeverity() { return s_state.dumpSeverity; }
bool IsDumpActive() { return s_state.dumpRemaining > 0.0f; }
bool IsDrivelineOpen(int gear) {
  return gear == 0 || s_state.disengagement > 0.40f;
}
const State &GetState() { return s_state; }

} // namespace ClutchSystem
