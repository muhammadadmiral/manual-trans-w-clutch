#pragma once

using Vehicle = int;

namespace MaintenanceSystem {

struct State {
  float oilLife = 1.0f;
  float oilLevel = 1.0f;
  float odometerKm = 0.0f;
  float engineHours = 0.0f;
  bool serviceDue = false;
};

void Reset();
void SelectVehicle(Vehicle vehicle);
void Update(float rpm, float throttle, float speedKmH, float oilTemperature,
            bool engineOn);
void ServiceOil();
float GetPowerFactor();
const State &GetState();

} // namespace MaintenanceSystem
