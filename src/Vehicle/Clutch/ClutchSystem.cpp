#include "ClutchSystem.h"

#include "../VehicleData.h"
#include "../Maintenance/WorkshopTuning.h"
#include "../../Core/Config.h"
#include "../../Script/DrivingEventBus.h"
#include "../../../sdk/inc/natives.h"

#include <algorithm>
#include <cmath>

namespace ClutchSystem {
namespace {

State s_state;
bool s_overheatPublished = false;
bool s_slipPublished = false;

float Clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

float SmoothStep(float value) {
  const float t = Clamp01(value);
  return t * t * (3.0f - 2.0f * t);
}

} // namespace

void Reset() {
  s_state = State{};
  s_overheatPublished = false;
  s_slipPublished = false;
}

void ServiceClutch() {
  s_state.heat = 0.0f;
  s_state.overloadSlip = 0.0f;
  s_state.dumpSeverity = 0.0f;
  s_state.dumpRemaining = 0.0f;
  s_state.judder = 0.0f;
  s_state.slipping = false;
  s_state.overloaded = false;
  s_overheatPublished = false;
  s_slipPublished = false;
}

float UpdatePedal(VehicleData &data, float rawPedal, float throttle,
                  float rpm, int gear, int maxGear, bool engineOn) {
  const float freePlayEnd =
      std::clamp(Config::ClutchBiteStart, 0.02f, 0.80f);
  const float fullyOpenAt =
      std::clamp(Config::ClutchBiteEnd, freePlayEnd + 0.05f, 0.98f);
  const float targetDisengagement =
      SmoothStep((Clamp01(rawPedal) - freePlayEnd) /
                 (fullyOpenAt - freePlayEnd));
  const float dt = std::clamp(MISC::GET_FRAME_TIME(), 0.0f, 0.05f);

  // handling.meta's clutch rate remains the actuator speed. If that field is
  // unavailable, follow the pedal directly instead of inventing a vehicle
  // class response.
  const float nativeClutchRate =
      data.GetClutchChangeRateScaleUpShift();
  const bool nativeRateValid =
      std::isfinite(nativeClutchRate) && nativeClutchRate > 0.0f;
  const float response =
      nativeRateValid && dt > 0.0f
          ? 1.0f -
                std::exp(-nativeClutchRate *
                         WorkshopTuning::GetClutchBiteRateMultiplier() * dt)
          : 1.0f;
  s_state.actuatorDisengagement +=
      (targetDisengagement - s_state.actuatorDisengagement) *
      Clamp01(response);
  s_state.disengagement = Clamp01(s_state.actuatorDisengagement);
  s_state.engagement = 1.0f - s_state.disengagement;
  s_state.handlingClutchRate =
      nativeRateValid ? nativeClutchRate : 0.0f;
  s_state.handlingDriveForce = data.GetInitialDriveForce();
  s_state.biteRatePerSecond = s_state.handlingClutchRate;

  if (dt > 0.0001f) {
    s_state.releaseRate =
        (s_state.previousDisengagement - s_state.disengagement) / dt;
  } else {
    s_state.releaseRate = 0.0f;
  }
  s_state.previousDisengagement = s_state.disengagement;

  const float dumpRate = std::max(1.0f, Config::ClutchDumpRate);
  const float freshDump =
      Clamp01((s_state.releaseRate - dumpRate) / dumpRate) *
      (0.35f + Clamp01(throttle) * 0.65f) * s_state.engagement;
  if (engineOn && gear != 0 && freshDump > 0.05f) {
    const bool wasActive = s_state.dumpRemaining > 0.0f;
    s_state.dumpSeverity = std::max(s_state.dumpSeverity, freshDump);
    s_state.dumpRemaining = 0.16f;
    if (!wasActive) {
      DrivingEventBus::EventData event{};
      event.vehicle = WorkshopTuning::GetState().vehicle;
      event.severity = s_state.dumpSeverity;
      DrivingEventBus::Publish(
          DrivingEventBus::Event::ClutchDump, event);
    }
  } else if (s_state.dumpRemaining > 0.0f) {
    s_state.dumpRemaining =
        std::max(0.0f, s_state.dumpRemaining - dt);
  } else {
    s_state.dumpSeverity = 0.0f;
  }

  s_state.torqueDemand =
      engineOn && gear != 0
          ? Clamp01(throttle) * s_state.engagement
          : 0.0f;
  s_state.torqueCapacity = 1.0f;
  s_state.packageCapacityMultiplier = 1.0f;
  s_state.overloadSlip = 0.0f;
  s_state.overloaded = false;
  s_state.judder = 0.0f;
  s_state.slipping =
      engineOn && gear != 0 && s_state.engagement > 0.0f &&
      s_state.engagement < 0.995f;

  if (s_state.slipping) {
    const float slipEnergy =
        (1.0f - s_state.engagement) *
        (0.25f + Clamp01(throttle) * 0.75f);
    s_state.heat +=
        slipEnergy * std::max(0.0f, Config::ClutchHeatRate) * dt;
  } else {
    s_state.heat -=
        std::max(0.0f, Config::ClutchCoolRate) * dt;
  }
  s_state.heat = Clamp01(s_state.heat);

  if (s_state.slipping && !s_slipPublished) {
    DrivingEventBus::EventData event{};
    event.vehicle = WorkshopTuning::GetState().vehicle;
    event.severity = 1.0f - s_state.engagement;
    DrivingEventBus::Publish(
        DrivingEventBus::Event::ClutchSlip, event);
    s_slipPublished = true;
  } else if (!s_state.slipping) {
    s_slipPublished = false;
  }
  if (s_state.heat > 0.85f && !s_overheatPublished) {
    DrivingEventBus::EventData event{};
    event.vehicle = WorkshopTuning::GetState().vehicle;
    event.severity = s_state.heat;
    DrivingEventBus::Publish(
        DrivingEventBus::Event::ClutchOverheat, event);
    s_overheatPublished = true;
  } else if (s_state.heat < 0.72f) {
    s_overheatPublished = false;
  }

  (void)rpm;
  (void)maxGear;
  return s_state.disengagement;
}

void ApplyToVehicle(VehicleData &data, int gear, float speedMps) {
  const bool drivelineOpen =
      gear == 0 || s_state.disengagement >= 0.88f;
  if (drivelineOpen) {
    s_state.nativeActuator =
        std::fabs(speedMps) < 1.0f ? -5.0f : -0.5f;
  } else {
    s_state.nativeActuator = s_state.engagement;
  }
  data.SetClutch(s_state.nativeActuator);
}

float GetEngagement() { return s_state.engagement; }
float GetHeat() { return s_state.heat; }
float GetNativeActuator() { return s_state.nativeActuator; }
float GetDumpSeverity() { return s_state.dumpSeverity; }
bool IsDumpActive() { return s_state.dumpRemaining > 0.0f; }
bool IsDrivelineOpen(int gear) {
  return gear == 0 || s_state.disengagement > 0.35f;
}
const State &GetState() { return s_state; }

} // namespace ClutchSystem
