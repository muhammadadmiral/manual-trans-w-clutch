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
  return selector == Selector::Drive || selector == Selector::Sport ||
         selector == Selector::Low2 || selector == Selector::Low1;
}

static float ResolveFlatVelocity(Vehicle vehicle, VehicleData &data,
                                 int maxGear) {
  const float memoryValue = std::fabs(data.GetDriveMaxFlatVel());
  if (std::isfinite(memoryValue) && memoryValue > 1.0f)
    return memoryValue;
  const float estimatedTop =
      (std::max)(8.0f, VEHICLE::GET_VEHICLE_ESTIMATED_MAX_SPEED(vehicle));
  const float topRatio = std::fabs(
      data.GetGearRatio(static_cast<uint8_t>((std::max)(1, maxGear))));
  return topRatio > 0.01f ? estimatedTop * topRatio : estimatedTop;
}

static float RoadRPM(Vehicle vehicle, VehicleData &data, int maxGear,
                     int gear, float signedSpeedMps) {
  if (gear < 1)
    return 0.2f;
  const float ratio =
      std::fabs(data.GetGearRatio(static_cast<uint8_t>(gear)));
  const float flatVelocity =
      ResolveFlatVelocity(vehicle, data, maxGear);
  if (ratio <= 0.01f || flatVelocity <= 1.0f)
    return std::clamp(data.GetRPM(), 0.0f, 1.0f);
  return std::clamp(std::fabs(signedSpeedMps) * ratio / flatVelocity,
                    0.0f, 1.25f);
}

static bool CanSelect(Vehicle vehicle, Selector from, Selector target,
                      float brake, float signedSpeedMps) {
  const float absSpeed = std::fabs(signedSpeedMps);
  if (target == Selector::Park && absSpeed > 0.8f)
    return false;
  if (target == Selector::Reverse && signedSpeedMps > 0.8f)
    return false;
  if (IsDriveSelector(target) && signedSpeedMps < -0.8f)
    return false;
  const float estimatedTop =
      (std::max)(8.0f, VEHICLE::GET_VEHICLE_ESTIMATED_MAX_SPEED(vehicle));
  if (target == Selector::Low2 && absSpeed > estimatedTop * 0.48f)
    return false;
  if (target == Selector::Low1 && absSpeed > estimatedTop * 0.25f)
    return false;

  if (!Config::AutomaticBrakeInterlock)
    return true;

  const bool leavingPark = from == Selector::Park;
  const bool selectingDirection =
      target == Selector::Reverse ||
      (IsDriveSelector(target) && !IsDriveSelector(from));
  return (!leavingPark && !selectingDirection) || brake >= 0.25f;
}

static bool DownshiftIsSafe(Vehicle vehicle, VehicleData &data,
                            int vehicleMaxGear, int fromGear, int toGear,
                            float signedSpeedMps) {
  if (toGear < 1 || fromGear <= toGear)
    return false;
  const float fromRatio =
      std::fabs(data.GetGearRatio(static_cast<uint8_t>(fromGear)));
  const float toRatio =
      std::fabs(data.GetGearRatio(static_cast<uint8_t>(toGear)));
  if (fromRatio <= 0.01f || toRatio <= 0.01f)
    return true;
  const float projectedRPM =
      RoadRPM(vehicle, data, vehicleMaxGear, toGear, signedSpeedMps);
  if (projectedRPM >= 0.98f)
    return false;

  const float topRatio = std::fabs(
      data.GetGearRatio(static_cast<uint8_t>((std::max)(1, vehicleMaxGear))));
  if (topRatio > 0.01f) {
    const float estimatedTop =
        (std::max)(8.0f, VEHICLE::GET_VEHICLE_ESTIMATED_MAX_SPEED(vehicle));
    const float targetGearLimit = estimatedTop * topRatio / toRatio;
    if (std::fabs(signedSpeedMps) > targetGearLimit * 0.97f)
      return false;
  }
  return true;
}

void Reset(Selector initialSelector) {
  s_state = State{};
  s_state.selector = initialSelector;
  s_state.lastShiftTime = GetTickCount();
}

void UpdateSelector(Vehicle vehicle, bool selectorUp, bool selectorDown,
                    float brake, float signedSpeedMps) {
  s_state.selectorRejected = false;
  if (selectorUp == selectorDown)
    return;

  // LShift maju di gate P-R-N-D-S-L2-L1, LCtrl balik.
  const int delta = selectorUp ? 1 : -1;
  const int current = static_cast<int>(s_state.selector);
  const int requested = std::clamp(current + delta, 0, 6);
  if (requested == current)
    return;

  const Selector target = static_cast<Selector>(requested);
  if (!CanSelect(vehicle, s_state.selector, target, brake, signedSpeedMps)) {
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

  const int vehicleMaxGear = (std::max)(1, maxGear);
  maxGear = vehicleMaxGear;
  if (s_state.selector == Selector::Low2)
    maxGear = (std::min)(2, maxGear);
  else if (s_state.selector == Selector::Low1)
    maxGear = 1;
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
  const DWORD elapsedSinceShift = now - s_state.lastShiftTime;
  if (elapsedSinceShift < delayMs)
    return s_state.currentGear;

  const bool sport = s_state.selector == Selector::Sport;
  const float nativeRPM = data.GetRPM();
  const float rpm =
      RoadRPM(vehicle, data, vehicleMaxGear, s_state.currentGear,
              signedSpeedMps);
  s_state.decisionRPM = rpm;
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
  const float estimatedTopSpeed =
      (std::max)(8.0f, VEHICLE::GET_VEHICLE_ESTIMATED_MAX_SPEED(vehicle));
  const float upshiftMinSpeed =
      estimatedTopSpeed * static_cast<float>(s_state.currentGear) *
      (sport ? 0.060f : 0.050f);
  const bool nativeLimiterPressure =
      throttle > 0.70f && nativeRPM > 0.78f && rpm > 0.62f;

  int targetGear = s_state.currentGear;
  if (kickdownRequest &&
      DownshiftIsSafe(vehicle, data, vehicleMaxGear, s_state.currentGear,
                      s_state.currentGear - 1, signedSpeedMps)) {
    targetGear = s_state.currentGear - 1;
    s_state.kickdown = true;
  } else if ((rpm > upThreshold || nativeLimiterPressure) &&
             s_state.currentGear < maxGear &&
             throttle > 0.04f &&
             std::fabs(signedSpeedMps) >= upshiftMinSpeed) {
    targetGear = s_state.currentGear + 1;
  } else if ((rpm < downThreshold || (brake > 0.35f && rpm < upThreshold)) &&
             s_state.currentGear > 1 &&
             DownshiftIsSafe(vehicle, data, vehicleMaxGear,
                             s_state.currentGear,
                             s_state.currentGear - 1, signedSpeedMps)) {
    targetGear = s_state.currentGear - 1;
  }

  if (targetGear != s_state.currentGear) {
    const int direction = targetGear > s_state.currentGear ? 1 : -1;
    const DWORD reversalHoldMs =
        (std::max<DWORD>)(900, delayMs * 3);
    if (s_state.lastShiftDirection != 0 &&
        direction != s_state.lastShiftDirection &&
        elapsedSinceShift < reversalHoldMs) {
      return s_state.currentGear;
    }

    const int previous = s_state.currentGear;
    s_state.currentGear = targetGear;
    s_state.lastShiftTime = now;
    s_state.lastShiftDirection = direction;
    GearboxSystem::NotifyAutomaticShift(data, previous, targetGear, sport);
    LOG_INFO(Gear,
             "Automatic %s shift: %d -> %d roadRPM=%.3f nativeRPM=%.3f "
             "throttle=%.3f",
             sport ? "S" : "D", previous, targetGear, rpm, nativeRPM,
             throttle);
  }

  (void)vehicle;
  return s_state.currentGear;
}

void ApplyToMemory(Vehicle vehicle, VehicleData &data, int activeGear) {
  const float openClutch =
      ENTITY::GET_ENTITY_SPEED(vehicle) < 1.0f ? -5.0f : -0.5f;
  if (s_state.selector == Selector::Park) {
    data.SetGear(1);
    data.SetNextGear(1);
    data.SetClutch(openClutch);
    VEHICLE::SET_VEHICLE_HANDBRAKE(vehicle, TRUE);
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 76, 1.0f);
    return;
  }

  if (activeGear == 0) {
    data.SetGear(1);
    data.SetNextGear(1);
    data.SetClutch(openClutch);
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
  case Selector::Low2:
    return "L2";
  case Selector::Low1:
    return "L1";
  }
  return "?";
}

const State &GetState() { return s_state; }

} // namespace AutomaticGearbox
