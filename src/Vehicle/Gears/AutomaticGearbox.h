#pragma once

#include <Windows.h>

using Vehicle = int;
class VehicleData;

namespace AutomaticGearbox {

enum class Selector : int {
  Park = 0,
  Reverse = 1,
  Neutral = 2,
  Drive = 3,
  Sport = 4,
  Low2 = 5,
  Low1 = 6
};

enum class ShiftPhase : int {
  Engaged = 0,
  Disengaging,
  Synchronizing,
  Engaging
};

struct State {
  Selector selector = Selector::Park;
  int currentGear = 1;
  int shiftFromGear = 1;
  int pendingGear = 1;
  DWORD lastShiftTime = 0;
  DWORD phaseStartedAt = 0;
  int lastShiftDirection = 0;
  float coupling = 0.0f;
  float decisionRPM = 0.2f;
  float shiftTargetRPM = 0.2f;
  float inputThrottle = 0.0f;
  ShiftPhase shiftPhase = ShiftPhase::Engaged;
  bool kickdown = false;
  bool rpmRecovery = false;
  bool selectorRejected = false;
};

void Reset(Selector initialSelector = Selector::Park);
void UpdateSelector(Vehicle vehicle, bool selectorUp, bool selectorDown,
                    float brake, float signedSpeedMps);
int Update(Vehicle vehicle, VehicleData &data, int maxGear, float throttle,
           float brake, float signedSpeedMps, bool engineOn);
void ApplyToMemory(Vehicle vehicle, VehicleData &data, int activeGear,
                   float driveThrottle);

Selector GetSelector();
int GetCurrentGear();
float GetCoupling();
float GetClutchDisengagement();
bool IsSport();
bool IsKickdownActive();
bool IsShifting();
bool WasSelectorRejected();
const char *GetSelectorName();
const char *GetShiftPhaseName();
const State &GetState();

} // namespace AutomaticGearbox
