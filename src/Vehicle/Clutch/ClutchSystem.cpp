#include "ClutchSystem.h"
#include "../VehicleData.h"
#include "../VehicleUpgrades.h"
#include "../Maintenance/WorkshopTuning.h"
#include "../../Core/Config.h"
#include "../../Script/DrivingEventBus.h"
#include "../../../sdk/inc/natives.h"
#include <algorithm>
#include <cmath>

namespace ClutchSystem {

static State s_state;
static bool s_overheatPublished = false;
static bool s_slipPublished = false;

static float Clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

static float SmoothStep(float value) {
  value = Clamp01(value);
  return value * value * (3.0f - 2.0f * value);
}

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

  const float travel =
      (Clamp01(rawPedal) - freePlayEnd) / (fullyOpenAt - freePlayEnd);
  const float dt = std::clamp(MISC::GET_FRAME_TIME(), 0.0f, 0.05f);
  const float targetDisengagement = SmoothStep(travel);
  float clutchRate = data.GetClutchChangeRateScaleUpShift();
  if (clutchRate <= 0.0f)
    clutchRate = 2.0f;
  float driveForce = data.GetInitialDriveForce();
  if (driveForce <= 0.0f)
    driveForce = 0.30f;
  const float rateFactor =
      std::sqrt(std::clamp(clutchRate, 0.20f, 20.0f) / 2.0f);
  const float torqueFactor =
      std::sqrt(std::clamp(driveForce, 0.05f, 1.50f) / 0.30f);
  const float biteRate =
      std::clamp(
          9.0f * rateFactor / torqueFactor *
              WorkshopTuning::GetClutchBiteRateMultiplier(),
          2.5f, 34.0f);
  const bool opening =
      targetDisengagement > s_state.actuatorDisengagement;
  const float responseRate = opening ? biteRate * 1.65f : biteRate;
  const float response =
      dt > 0.0f ? 1.0f - std::exp(-responseRate * dt) : 1.0f;
  s_state.actuatorDisengagement +=
      (targetDisengagement - s_state.actuatorDisengagement) *
      Clamp01(response);
  float disengagement = Clamp01(s_state.actuatorDisengagement);
  float engagement = 1.0f - disengagement;
  s_state.handlingDriveForce = driveForce;
  s_state.handlingClutchRate = clutchRate;
  s_state.biteRatePerSecond = biteRate;

  if (dt > 0.0001f) {
    s_state.releaseRate =
        (s_state.previousDisengagement - disengagement) / dt;
  } else {
    s_state.releaseRate = 0.0f;
  }
  const float dumpRate = std::max(1.0f, Config::ClutchDumpRate);
  const float freshDump =
      Clamp01((s_state.releaseRate - dumpRate) / dumpRate) *
      (0.35f + Clamp01(throttle) * 0.65f) * engagement;
  if (engineOn && freshDump > 0.05f) {
    const bool wasDumpActive = s_state.dumpRemaining > 0.0f;
    s_state.dumpSeverity =
        std::max(s_state.dumpSeverity, freshDump);
    s_state.dumpRemaining = 0.16f;
    if (!wasDumpActive) {
      DrivingEventBus::EventData event{};
      event.vehicle = WorkshopTuning::GetState().vehicle;
      event.severity = s_state.dumpSeverity;
      DrivingEventBus::Publish(
          DrivingEventBus::Event::ClutchDump, event);
    }
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

  const float gearLoad =
      gear == 0
          ? 0.0f
          : Clamp01(static_cast<float>((std::max)(0, std::abs(gear) - 1)) /
                    static_cast<float>((std::max)(1, maxGear - 1)));
  const float lugLoad = 1.0f - SmoothStep((rpm - 0.12f) / 0.38f);
  const float upgradeTorque =
      1.0f + VehicleUpgrades::GetState().engineStage * 0.14f;
  s_state.torqueDemand =
      engineOn && gear != 0
          ? Clamp01(throttle) * upgradeTorque *
                (0.72f + gearLoad * 0.38f + lugLoad * gearLoad * 0.35f)
          : 0.0f;
  const float heatFade =
      1.0f - Clamp01((s_state.heat -
                      std::clamp(Config::ClutchFadeStart, 0.50f, 0.99f)) /
                     std::max(0.01f, 1.0f - Config::ClutchFadeStart)) *
                 Clamp01(Config::ClutchFadeStrength);
  s_state.torqueCapacity =
      std::max(0.05f, Config::MaxClutchTorque) *
      WorkshopTuning::GetClutchCapacityMultiplier() *
      (0.08f + engagement * 0.92f) * heatFade;
  s_state.packageCapacityMultiplier =
      WorkshopTuning::GetClutchCapacityMultiplier();
  const float overloadTarget =
      engineOn && engagement > 0.08f &&
              s_state.torqueDemand > s_state.torqueCapacity
          ? Clamp01((s_state.torqueDemand - s_state.torqueCapacity) /
                    std::max(0.10f, s_state.torqueCapacity))
          : 0.0f;
  const float overloadRate =
      overloadTarget > s_state.overloadSlip ? 8.0f : 3.0f;
  s_state.overloadSlip +=
      (overloadTarget - s_state.overloadSlip) *
      Clamp01(dt * overloadRate);
  s_state.overloaded = s_state.overloadSlip > 0.02f;
  engagement *= 1.0f - s_state.overloadSlip * 0.55f;
  disengagement = 1.0f - engagement;

  s_state.slipping =
      engineOn && engagement > 0.01f &&
      (engagement < 0.99f || s_state.overloaded);
  if (s_state.slipping) {
    const float slipEnergy =
        ((1.0f - engagement) + s_state.overloadSlip * 0.80f) *
        (0.25f + Clamp01(throttle) * 0.75f);
    s_state.heat +=
        slipEnergy * std::max(0.0f, Config::ClutchHeatRate) *
        WorkshopTuning::GetClutchHeatMultiplier() * dt;
  } else {
    s_state.heat -=
        std::max(0.0f, Config::ClutchCoolRate) *
        WorkshopTuning::GetClutchCoolingMultiplier() * dt;
  }
  s_state.heat = Clamp01(s_state.heat);
  if (s_state.slipping && !s_slipPublished) {
    DrivingEventBus::EventData event{};
    event.vehicle = WorkshopTuning::GetState().vehicle;
    event.severity =
        Clamp01((1.0f - engagement) * 0.55f +
                s_state.overloadSlip * 0.75f);
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

  const float fadeStart =
      std::clamp(Config::ClutchFadeStart, 0.50f, 0.99f);
  if (s_state.heat > fadeStart) {
    const float fade = (s_state.heat - fadeStart) / (1.0f - fadeStart);
    engagement *=
        1.0f - Clamp01(Config::ClutchFadeStrength) * Clamp01(fade);
    disengagement = 1.0f - engagement;
  }

  s_state.judder = 0.0f;
  if (Config::ClutchJudder && s_state.heat > fadeStart &&
      engagement > 0.12f && engagement < 0.88f &&
      s_state.torqueDemand > 0.10f) {
    const float heatSeverity =
        Clamp01((s_state.heat - fadeStart) / (1.0f - fadeStart));
    const float biteBand =
        1.0f - std::fabs(engagement - 0.50f) * 2.0f;
    s_state.judderPhase =
        std::fmod(s_state.judderPhase + dt * (34.0f + throttle * 18.0f),
                  6.2831853f);
    s_state.judder =
        std::sin(s_state.judderPhase) * heatSeverity *
        Clamp01(biteBand) * 0.13f;
    engagement = Clamp01(engagement * (1.0f + s_state.judder));
    disengagement = 1.0f - engagement;
    PAD::SET_CONTROL_SHAKE(
        0, 70, static_cast<int>(55.0f + heatSeverity * 120.0f));
  }

  s_state.disengagement = disengagement;
  s_state.engagement = engagement;
  return disengagement;
}

void ApplyToVehicle(VehicleData &data, int gear, float speedMps) {
  const bool drivelineOpen = s_state.disengagement >= 0.88f;

  if (gear == 0 || drivelineOpen) {
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
