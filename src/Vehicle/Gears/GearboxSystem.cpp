#include "GearboxSystem.h"
#include "../VehicleData.h"
#include "../../Core/Config.h"
#include "../../../sdk/inc/natives.h"
#include <algorithm>
#include <cmath>

namespace GearboxSystem {

static State s_state;

void Reset() {
  s_state = State{};
}

void Update(Vehicle vehicle, VehicleData &data, int gear, int maxGear,
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

  if (s_state.pendingEngagement && clutchDisengagement >= 0.35f) {
    s_state.torqueCut = 0.0f;
  } else if (s_state.shockRemaining > 0.0f) {
    s_state.pendingEngagement = false;
    s_state.shockRemaining =
        std::max(0.0f, s_state.shockRemaining - dt);
    const float envelope =
        std::clamp(s_state.shockRemaining / 0.20f, 0.0f, 1.0f);
    s_state.torqueCut =
        std::clamp(Config::ShiftShockStrength, 0.0f, 1.0f) *
        s_state.clashSeverity * envelope;

    if (gear > 0 && throttle > 0.01f && s_state.torqueCut > 0.01f) {
      PAD::DISABLE_CONTROL_ACTION(0, 71, true);
      PAD::SET_CONTROL_VALUE_NEXT_FRAME(
          0, 71, throttle * (1.0f - std::min(0.80f, s_state.torqueCut)));
    }
    if (s_state.clashSeverity > 0.15f)
      PAD::SET_CONTROL_SHAKE(0, 90,
          static_cast<int>(80.0f + s_state.clashSeverity * 120.0f));
    if (s_state.moneyShift && !s_state.damageApplied) {
      const float currentHealth =
          VEHICLE::GET_VEHICLE_ENGINE_HEALTH(vehicle);
      const float damage =
          std::max(0.0f, Config::OverRevShiftDamage) *
          (160.0f + s_state.moneyShiftSeverity * 340.0f);
      VEHICLE::SET_VEHICLE_ENGINE_HEALTH(
          vehicle, std::max(-4000.0f, currentHealth - damage));
      s_state.damageApplied = true;
    }
  } else {
    s_state.torqueCut = 0.0f;
    s_state.clashActive = false;
    s_state.moneyShift = false;
  }
}

void NotifyGrind() {
  s_state.health =
      std::max(0.0f, s_state.health - std::max(0.0f, Config::GearGrindDamage));
}

void NotifyShift(VehicleData &data, int fromGear, int toGear,
                 float clutchDisengagement, float throttle) {
  s_state.lastFromGear = fromGear;
  s_state.lastToGear = toGear;

  const uint8_t fromIndex =
      fromGear < 0 ? 0 : static_cast<uint8_t>(fromGear);
  const uint8_t toIndex =
      toGear < 0 ? 0 : static_cast<uint8_t>(toGear);
  const float fromRatio = std::fabs(data.GetGearRatio(fromIndex));
  const float toRatio = std::fabs(data.GetGearRatio(toIndex));
  const float rpm = data.GetRPM();

  s_state.shiftTargetRPM = rpm;
  s_state.moneyShift = false;
  s_state.moneyShiftSeverity = 0.0f;
  s_state.damageApplied = false;
  if (fromGear != 0 && toGear != 0 && fromRatio > 0.01f &&
      toRatio > 0.01f) {
    const float rawTarget = rpm * toRatio / fromRatio;
    s_state.moneyShift = rawTarget > 1.0f;
    s_state.moneyShiftSeverity =
        std::clamp(rawTarget - 1.0f, 0.0f, 2.0f);
    s_state.shiftTargetRPM =
        std::clamp(rawTarget, 0.15f, 1.0f);
    if (s_state.moneyShift) {
      const float overSpeed = std::clamp(rawTarget - 1.0f, 0.0f, 1.0f);
      s_state.health = std::max(
          0.0f, s_state.health -
                    std::max(0.0f, Config::OverRevShiftDamage) *
                        (0.5f + overSpeed));
    }
  }
  if (fromGear != 0 && toGear != 0 &&
      (fromRatio <= 0.01f || toRatio <= 0.01f)) {
    const float ratioApprox =
        static_cast<float>((std::max)(1, std::abs(fromGear))) /
        static_cast<float>((std::max)(1, std::abs(toGear)));
    const float rawTarget = rpm * ratioApprox;
    s_state.moneyShift = toGear > 0 && toGear < fromGear && rawTarget > 1.0f;
    s_state.moneyShiftSeverity =
        std::clamp(rawTarget - 1.0f, 0.0f, 2.0f);
    s_state.shiftTargetRPM = std::clamp(rawTarget, 0.15f, 1.0f);
  }
  s_state.syncError = std::fabs(s_state.shiftTargetRPM - rpm);

  const bool clutchless = clutchDisengagement < 0.35f;
  if (fromGear == 0 && toGear != 0 && clutchless && throttle < 0.45f)
    s_state.stallRequest = true;
  const float noLift =
      throttle * std::clamp(Config::NoLiftShiftPenalty, 0.0f, 1.0f);
  s_state.clashSeverity = std::clamp(
      (clutchless ? 0.55f : 0.0f) + s_state.syncError * 1.25f + noLift,
      0.0f, 1.0f);
  if (s_state.moneyShift)
    s_state.clashSeverity = 1.0f;
  s_state.clashActive = Config::GearClash && s_state.clashSeverity > 0.05f;
  s_state.shockRemaining =
      s_state.clashActive ? 0.10f + 0.14f * s_state.clashSeverity : 0.0f;
  s_state.pendingEngagement =
      s_state.clashActive && clutchDisengagement >= 0.35f;

  if (clutchless && Config::GearClash) {
    s_state.health = std::max(
        0.0f, s_state.health -
                  std::max(0.0f, Config::GearGrindDamage) *
                      (0.5f + s_state.clashSeverity));
  }
}

void NotifyAutomaticShift(VehicleData &data, int fromGear, int toGear,
                          bool sportMode) {
  const float fromRatio =
      std::fabs(data.GetGearRatio(static_cast<uint8_t>(fromGear)));
  const float toRatio =
      std::fabs(data.GetGearRatio(static_cast<uint8_t>(toGear)));
  const float rpm = data.GetRPM();

  s_state.lastFromGear = fromGear;
  s_state.lastToGear = toGear;
  s_state.moneyShift = false;
  s_state.shiftTargetRPM =
      fromRatio > 0.01f && toRatio > 0.01f
          ? std::clamp(rpm * toRatio / fromRatio, 0.15f, 1.0f)
          : rpm;
  s_state.syncError = std::fabs(s_state.shiftTargetRPM - rpm);
  s_state.clashSeverity =
      std::clamp((sportMode ? 0.16f : 0.08f) +
                     s_state.syncError * (sportMode ? 0.35f : 0.20f),
                 0.0f, 0.45f);
  s_state.clashActive = false;
  s_state.pendingEngagement = false;
  s_state.shockRemaining = sportMode ? 0.12f : 0.16f;
}

void NotifyRevMatch(float currentRPM, float targetRPM) {
  s_state.revMatchTarget = targetRPM;
  s_state.revMatched = std::fabs(currentRPM - targetRPM) < 0.15f;
}

float GetHealth() { return s_state.health; }
bool IsSeized() { return s_state.health <= 0.0f; }
bool ConsumeStallRequest() {
  const bool requested = s_state.stallRequest;
  s_state.stallRequest = false;
  return requested;
}
const State &GetState() { return s_state; }

} // namespace GearboxSystem
