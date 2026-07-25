#pragma once

class VehicleData;

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
  bool revMatched = false;
  bool clashActive = false;
  bool pendingEngagement = false;
  int lastFromGear = 0;
  int lastToGear = 0;
};

void Reset();
void Update(VehicleData &data, int gear, int maxGear,
            float clutchDisengagement, float throttle, bool engineOn);
void NotifyGrind();
void NotifyShift(VehicleData &data, int fromGear, int toGear,
                 float clutchDisengagement, float throttle);
void NotifyAutomaticShift(VehicleData &data, int fromGear, int toGear,
                          bool sportMode);
void NotifyRevMatch(float currentRPM, float targetRPM);

float GetHealth();
bool IsSeized();
const State &GetState();

} // namespace GearboxSystem
