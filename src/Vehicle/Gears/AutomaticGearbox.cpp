#include "AutomaticGearbox.h"
#include "GearboxSystem.h"
#include "../VehicleData.h"
#include "../../Core/Config.h"
#include "../../Core/ModLogger.h"
#include "../../../sdk/inc/natives.h"
#include <algorithm>
#include <cmath>

namespace AutomaticGearbox {

static State s_state;

static float Clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

static bool IsDriveSelector(Selector selector) {
  return selector == Selector::Drive || selector == Selector::Sport;
}

static bool CanSelect(Selector from, Selector target, float brake,
                      float signedSpeedMps) {
  const float absSpeed = std::fabs(signedSpeedMps);
  if (target == Selector::Park && absSpeed > 0.8f)
    return false;
  if (target == Selector::Reverse && signedSpeedMps > 0.8f)
    return false;
  if (IsDriveSelector(target) && signedSpeedMps < -0.8f)
    return false;

  if (!Config::AutomaticBrakeInterlock)
    return true;

  const bool leavingPark = from == Selector::Park;
  const bool selectingDirection =
      target == Selector::Reverse ||
      (IsDriveSelector(target) && !IsDriveSelector(from));
  return (!leavingPark && !selectingDirection) || brake >= 0.25f;
}

static bool DownshiftIsSafe(VehicleData &data, int fromGear, int toGear) {
  if (toGear < 1 || fromGear <= toGear)
    return false;
  const float fromRatio =
      std::fabs(data.GetGearRatio(static_cast<uint8_t>(fromGear)));
  const float toRatio =
      std::fabs(data.GetGearRatio(static_cast<uint8_t>(toGear)));
  if (fromRatio <= 0.01f || toRatio <= 0.01f)
    return true;
  return data.GetRPM() * toRatio / fromRatio < 0.98f;
}

void Reset() {
  s_state = State{};
  s_state.lastShiftTime = GetTickCount();
}

void UpdateSelector(Vehicle vehicle, bool selectorUp, bool selectorDown,
                    float brake, float signedSpeedMps) {
  s_state.selectorRejected = false;
  if (selectorUp == selectorDown)
    return;

  const int delta = selectorDown ? 1 : -1;
  const int current = static_cast<int>(s_state.selector);
  const int requested = std::clamp(current + delta, 0, 4);
  if (requested == current)
    return;

  const Selector target = static_cast<Selector>(requested);
  if (!CanSelect(s_state.selector, target, brake, signedSpeedMps)) {
    s_state.selectorRejected = true;
    HUD::BEGIN_TEXT_COMMAND_THEFEED_POST("STRING");
    HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(
        "~r~Selector locked~w~ - tekan rem / turunkan kecepatan");
    HUD::END_TEXT_COMMAND_THEFEED_POST_TICKER(false, false);
    return;
  }

  const Selector previous = s_state.selector;
  s_state.selector = target;
  s_state.kickdown = false;
  if (IsDriveSelector(target) && !IsDriveSelector(previous))
    s_state.currentGear = 1;

  if (previous == Selector::Park && target != Selector::Park)
    VEHICLE::SET_VEHICLE_HANDBRAKE(vehicle, FALSE);

  LOG_INFO(Gear, "Automatic selector: %d -> %d",
           static_cast<int>(previous), static_cast<int>(target));
}

int Update(Vehicle vehicle, VehicleData &data, int maxGear, float throttle,
           float brake, float signedSpeedMps, bool engineOn) {
  throttle = Clamp01(throttle);
  s_state.kickdown = false;

  if (s_state.selector == Selector::Park ||
      s_state.selector == Selector::Neutral) {
    s_state.coupling = 0.0f;
    return 0;
  }
  if (s_state.selector == Selector::Reverse) {
    const float speedCoupling = Clamp01(std::fabs(signedSpeedMps) / 6.0f);
    s_state.coupling =
        engineOn ? std::clamp(0.62f + speedCoupling * 0.38f +
                                 throttle * 0.12f,
                             0.0f, 1.0f)
                 : 0.0f;
    return -1;
  }

  maxGear = (std::max)(1, maxGear);
  s_state.currentGear = std::clamp(s_state.currentGear, 1, maxGear);
  const float speedCoupling = Clamp01(std::fabs(signedSpeedMps) / 7.0f);
  s_state.coupling =
      engineOn ? std::clamp(0.62f + speedCoupling * 0.38f +
                               throttle * 0.10f,
                           0.0f, 1.0f)
               : 0.0f;

  const DWORD now = GetTickCount();
  const DWORD delayMs = static_cast<DWORD>(
      std::clamp(Config::AutomaticShiftDelay, 0.10f, 1.20f) * 1000.0f);
  if (now - s_state.lastShiftTime < delayMs)
    return s_state.currentGear;

  const bool sport = s_state.selector == Selector::Sport;
  const float rpm = data.GetRPM();
  const float upBase = sport ? Config::AutomaticSUpRPM
                             : Config::AutomaticDUpRPM;
  const float downBase = sport ? Config::AutomaticSDownRPM
                               : Config::AutomaticDDownRPM;
  const float upThreshold =
      std::clamp(upBase + throttle * (sport ? 0.06f : 0.18f),
                 0.35f, 0.99f);
  const float downThreshold =
      std::clamp(downBase + throttle * (sport ? 0.12f : 0.08f),
                 0.10f, upThreshold - 0.08f);
  const bool kickdownRequest =
      throttle >= std::clamp(Config::AutomaticKickdownThrottle, 0.40f, 0.98f) &&
      rpm < (sport ? 0.82f : 0.72f) && s_state.currentGear > 1;

  int targetGear = s_state.currentGear;
  if (kickdownRequest &&
      DownshiftIsSafe(data, s_state.currentGear, s_state.currentGear - 1)) {
    targetGear = s_state.currentGear - 1;
    s_state.kickdown = true;
  } else if (rpm > upThreshold && s_state.currentGear < maxGear &&
             throttle > 0.04f) {
    targetGear = s_state.currentGear + 1;
  } else if ((rpm < downThreshold || (brake > 0.35f && rpm < upThreshold)) &&
             s_state.currentGear > 1 &&
             DownshiftIsSafe(data, s_state.currentGear,
                             s_state.currentGear - 1)) {
    targetGear = s_state.currentGear - 1;
  }

  if (targetGear != s_state.currentGear) {
    const int previous = s_state.currentGear;
    s_state.currentGear = targetGear;
    s_state.lastShiftTime = now;
    GearboxSystem::NotifyAutomaticShift(data, previous, targetGear, sport);
    LOG_INFO(Gear, "Automatic %s shift: %d -> %d rpm=%.3f throttle=%.3f",
             sport ? "S" : "D", previous, targetGear, rpm, throttle);
  }

  (void)vehicle;
  return s_state.currentGear;
}

void ApplyToMemory(Vehicle vehicle, VehicleData &data, int activeGear) {
  if (s_state.selector == Selector::Park) {
    data.SetGear(0xFF);
    data.SetNextGear(0xFF);
    data.SetClutch(-5.0f);
    VEHICLE::SET_VEHICLE_HANDBRAKE(vehicle, TRUE);
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 76, 1.0f);
    return;
  }

  if (activeGear == 0) {
    data.SetGear(0xFF);
    data.SetNextGear(0xFF);
    data.SetClutch(-5.0f);
  } else if (activeGear < 0) {
    data.SetGear(0);
    data.SetNextGear(0);
    data.SetClutch(s_state.coupling);
  } else {
    const uint8_t gear = static_cast<uint8_t>(activeGear);
    data.SetGear(gear);
    data.SetNextGear(gear);
    data.SetClutch(s_state.coupling);
  }
}

Selector GetSelector() { return s_state.selector; }
int GetCurrentGear() { return s_state.currentGear; }
float GetCoupling() { return s_state.coupling; }
float GetClutchDisengagement() { return 1.0f - s_state.coupling; }
bool IsSport() { return s_state.selector == Selector::Sport; }
bool IsKickdownActive() { return s_state.kickdown; }
bool WasSelectorRejected() { return s_state.selectorRejected; }

const char *GetSelectorName() {
  switch (s_state.selector) {
  case Selector::Park:
    return "P";
  case Selector::Reverse:
    return "R";
  case Selector::Neutral:
    return "N";
  case Selector::Drive:
    return "D";
  case Selector::Sport:
    return "S";
  }
  return "?";
}

const State &GetState() { return s_state; }

} // namespace AutomaticGearbox
