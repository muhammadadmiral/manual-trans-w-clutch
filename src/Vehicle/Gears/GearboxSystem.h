#pragma once

class VehicleData;

namespace GearboxSystem {

struct State {
  float health = 1.0f;
  float overRev = 0.0f;
  float revMatchTarget = 0.0f;
  bool revMatched = false;
};

void Reset();
void Update(VehicleData &data, int gear, int maxGear,
            float clutchDisengagement, float throttle, bool engineOn);
void NotifyGrind();
void NotifyRevMatch(float currentRPM, float targetRPM);

float GetHealth();
bool IsSeized();
const State &GetState();

} // namespace GearboxSystem
