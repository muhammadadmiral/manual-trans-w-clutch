#pragma once
#include <array>
#include <cstdint>

class VehicleData;
using Vehicle = int;

namespace GearboxSystem {

struct State {
  float health = 1.0f;
  float overRev = 0.0f;
  float revMatchTarget = 0.0f;
  float shiftTargetRPM = 0.0f;
  float syncError = 0.0f;
  float clashSeverity = 0.0f;
  float shockRemaining = 0.0f;
  float torqueCut = 0.0f;
  float shiftAssistCutRemaining = 0.0f;
  float penaltyMultiplier = 1.0f;
  bool revMatched = false;
  bool clashActive = false;
  bool pendingEngagement = false;
  bool moneyShift = false;
  bool damageApplied = false;
  bool stallRequest = false;
  bool quickShift = false;
  bool powerShift = false;
  bool synchroShift = false;
  float moneyShiftSeverity = 0.0f;
  float wheelLockRemaining = 0.0f;
  float wheelLockBrake = 0.0f;
  float selectedSynchroWear = 0.0f;
  uint32_t resistanceDelayMs = 0;
  bool shiftRejected = false;
  std::array<float, 9> synchroWear{};
  int lastFromGear = 0;
  int lastToGear = 0;
};

void Reset();
void ServiceGearbox();
void Update(Vehicle vehicle, VehicleData &data, int gear, int maxGear,
            float clutchDisengagement, float throttle, bool engineOn);
void NotifyGrind();
void NotifyShift(Vehicle vehicle, VehicleData &data, int fromGear, int toGear,
                 float clutchDisengagement, float throttle);
void NotifyAutomaticShift(VehicleData &data, int fromGear, int toGear,
                          bool sportMode);
void NotifyRevMatch(float currentRPM, float targetRPM);
uint32_t GetShiftResistanceMs(VehicleData &data, int fromGear, int toGear,
                              float clutchDisengagement, float throttle);

float GetHealth();
bool IsSeized();
bool ConsumeStallRequest();
float GetWheelLockBrake();
const State &GetState();

} // namespace GearboxSystem
