#include "AutomaticGearbox.h"

#include "../../VehicleData.h"
#include "../../../Core/Config.h"
#include "../../../Core/ModLogger.h"
#include "../../../Script/DrivingEventBus.h"
#include "../../../../sdk/inc/natives.h"

#include <algorithm>
#include <cmath>

namespace AutomaticGearbox {
namespace {

State s_state;

bool IsDriveSelector(Selector selector) {
  return selector == Selector::Drive || selector == Selector::Sport;
}

bool CanSelect(Selector from, Selector target, float brake,
               float signedSpeedMps) {
  if (target == Selector::Park && std::fabs(signedSpeedMps) > 0.8f)
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

int ReadNativeForwardGear(Vehicle vehicle, VehicleData &data, int maxGear) {
  const int nativeCurrent =
      VEHICLE::_GET_VEHICLE_CURRENT_DRIVE_GEAR(vehicle);
  if (nativeCurrent >= 1 && nativeCurrent <= maxGear)
    return nativeCurrent;

  const uint8_t memoryCurrent = data.GetGear();
  if (memoryCurrent >= 1 &&
      memoryCurrent <= static_cast<uint8_t>(maxGear)) {
    return static_cast<int>(memoryCurrent);
  }

  const int desired =
      VEHICLE::_GET_VEHICLE_DESIRED_DRIVE_GEAR(vehicle);
  if (desired >= 1 && desired <= maxGear)
    return desired;
  return 1;
}

} // namespace

void Reset(Selector initialSelector) {
  s_state = State{};
  s_state.selector = initialSelector;
  s_state.currentGear = 1;
  s_state.pendingGear = 1;
  s_state.lastShiftTime = GetTickCount();
  s_state.phaseStartedAt = s_state.lastShiftTime;
}

void ServiceTransmission() {
  s_state.fluidTemperature = 0.0f;
  s_state.brakeBoostTime = 0.0f;
  s_state.stallRequest = false;
  s_state.limpMode = false;
  s_state.neutralDrop = false;
  s_state.torqueManagement = 0.0f;
}

void UpdateSelector(Vehicle vehicle, bool selectorUp, bool selectorDown,
                    float brake, float signedSpeedMps, float engineRPM) {
  s_state.selectorRejected = false;
  if (selectorUp == selectorDown)
    return;

  // Native automatic owns all forward shift points. The mod only exposes
  // selector positions that map to real behavior without a scripted shift
  // map: P-R-N-D.
  const int delta = selectorUp ? 1 : -1;
  const int current = static_cast<int>(s_state.selector);
  const int requested = std::clamp(current + delta, 0, 3);
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
  s_state.shiftPhase = ShiftPhase::Engaged;
  s_state.kickdown = false;
  s_state.kickdownPending = false;
  s_state.ignitionCut = false;
  s_state.neutralDrop = false;
  s_state.safetyNeutral = false;
  s_state.phaseStartedAt = GetTickCount();

  if (previous == Selector::Park && target != Selector::Park)
    VEHICLE::SET_VEHICLE_HANDBRAKE(vehicle, FALSE);

  DrivingEventBus::EventData event{};
  event.vehicle = vehicle;
  event.fromGear = static_cast<int>(previous);
  event.toGear = static_cast<int>(target);
  DrivingEventBus::Publish(DrivingEventBus::Event::SelectorChanged, event);
  LOG_INFO(Gear, "Native automatic selector: %d -> %d rpm=%.3f",
           static_cast<int>(previous), static_cast<int>(target), engineRPM);
}

int Update(Vehicle vehicle, VehicleData &data, int maxGear, float throttle,
           float brake, float signedSpeedMps, bool engineOn) {
  s_state.inputThrottle = std::clamp(throttle, 0.0f, 1.0f);
  s_state.decisionRPM = data.GetRPM();
  s_state.shiftTargetRPM = s_state.decisionRPM;
  s_state.kickdown = false;
  s_state.rpmRecovery = false;
  s_state.ignitionCut = false;
  s_state.sportBlip = 0.0f;
  s_state.tccLocked = false;
  s_state.hillCreepFailure = false;
  s_state.limpMode = false;
  s_state.shiftPhase = ShiftPhase::Engaged;

  if (s_state.selector == Selector::Park ||
      s_state.selector == Selector::Neutral) {
    s_state.coupling = 0.0f;
    s_state.hydraulicCoupling = 0.0f;
    return 0;
  }
  if (s_state.selector == Selector::Reverse) {
    s_state.currentGear = -1;
    s_state.pendingGear = -1;
    s_state.coupling = engineOn ? 1.0f : 0.0f;
    s_state.hydraulicCoupling = s_state.coupling;
    return -1;
  }

  const int nativeMaxGear = std::max(1, maxGear);
  const int previousGear = s_state.currentGear;
  const int nativeGear =
      ReadNativeForwardGear(vehicle, data, nativeMaxGear);
  s_state.currentGear = nativeGear;
  s_state.pendingGear =
      std::clamp(VEHICLE::_GET_VEHICLE_DESIRED_DRIVE_GEAR(vehicle),
                 1, nativeMaxGear);
  s_state.coupling = engineOn ? 1.0f : 0.0f;
  s_state.hydraulicCoupling = s_state.coupling;

  if (previousGear > 0 && previousGear != nativeGear) {
    s_state.shiftFromGear = previousGear;
    s_state.lastShiftDirection = nativeGear > previousGear ? 1 : -1;
    s_state.lastShiftTime = GetTickCount();
    DrivingEventBus::EventData event{};
    event.vehicle = vehicle;
    event.fromGear = previousGear;
    event.toGear = nativeGear;
    DrivingEventBus::Publish(
        nativeGear > previousGear
            ? DrivingEventBus::Event::GearShiftUp
            : DrivingEventBus::Event::GearShiftDown,
        event);
    LOG_INFO(Gear, "Native automatic shift observed: %d -> %d rpm=%.3f",
             previousGear, nativeGear, s_state.decisionRPM);
  }

  (void)brake;
  (void)signedSpeedMps;
  return nativeGear;
}

void ApplyToMemory(Vehicle vehicle, VehicleData &data, int activeGear,
                   float driveThrottle) {
  const float openClutch =
      ENTITY::GET_ENTITY_SPEED(vehicle) < 1.0f ? -5.0f : -0.5f;
  if (s_state.selector == Selector::Park) {
    data.SetGear(1);
    data.SetNextGear(1);
    data.SetClutch(openClutch);
    VEHICLE::SET_VEHICLE_HANDBRAKE(vehicle, TRUE);
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 76, 1.0f);
  } else if (s_state.selector == Selector::Neutral) {
    data.SetGear(1);
    data.SetNextGear(1);
    data.SetClutch(openClutch);
  } else if (s_state.selector == Selector::Reverse) {
    data.SetGear(0);
    data.SetNextGear(0);
    data.SetClutch(s_state.coupling);
  } else {
    // D: no current/next gear, RPM, clutch or internal throttle writes.
    // CTaskVehicle and CVehicle keep their native automatic shift ownership.
    VEHICLE::SET_VEHICLE_HANDBRAKE(vehicle, FALSE);
  }
  (void)activeGear;
  (void)driveThrottle;
}

Selector GetSelector() { return s_state.selector; }
int GetCurrentGear() { return s_state.currentGear; }
float GetCoupling() { return s_state.hydraulicCoupling; }
float GetClutchDisengagement() {
  return 1.0f - s_state.hydraulicCoupling;
}
bool IsSport() { return s_state.selector == Selector::Sport; }
bool IsKickdownActive() { return false; }
bool IsShifting() { return false; }
bool WasSelectorRejected() { return s_state.selectorRejected; }

void ForceNeutral() {
  if (!IsDriveSelector(s_state.selector))
    return;
  s_state.selector = Selector::Neutral;
  s_state.currentGear = 0;
  s_state.pendingGear = 0;
  s_state.coupling = 0.0f;
  s_state.hydraulicCoupling = 0.0f;
  s_state.shiftPhase = ShiftPhase::Engaged;
  s_state.safetyNeutral = true;
  LOG_WARN(Gear, "Native automatic safety-neutral");
}

void SetTorqueManagement(float intervention) {
  s_state.torqueManagement = std::clamp(intervention, 0.0f, 1.0f);
}

bool ConsumeStallRequest() {
  const bool result = s_state.stallRequest;
  s_state.stallRequest = false;
  return result;
}

const char *GetSelectorName() {
  switch (s_state.selector) {
  case Selector::Park: return "P";
  case Selector::Reverse: return "R";
  case Selector::Neutral: return "N";
  case Selector::Drive: return "D";
  case Selector::Sport: return "S";
  case Selector::Low2: return "D";
  case Selector::Low1: return "D";
  }
  return "?";
}

const char *GetShiftPhaseName() {
  return "NATIVE";
}

const State &GetState() { return s_state; }

} // namespace AutomaticGearbox
