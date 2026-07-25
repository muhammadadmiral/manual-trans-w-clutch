#pragma once

class VehicleData;
using Vehicle = int;

namespace ClutchSystem {

struct State {
  float disengagement = 0.0f;
  float engagement = 1.0f;
  float heat = 0.0f;
  float nativeActuator = 1.0f;
  bool slipping = false;
};

void Reset();

// rawPedal: 0 dilepas, 1 diinjak penuh.
float UpdatePedal(float rawPedal, float throttle, bool engineOn);

// Gear tetap kepasang; field clutch asli yang mutus torsinya.
void ApplyToVehicle(VehicleData &data, int gear, float speedMps);

float GetEngagement();
float GetHeat();
float GetNativeActuator();
bool IsDrivelineOpen(int gear);
const State &GetState();

} // namespace ClutchSystem
