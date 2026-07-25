#pragma once

class VehicleData;
using Vehicle = int;

namespace BrakeSystem {

struct State {
  bool absEnabled = true;
  bool absActive = false;
  float absLevel = 0.0f;
  float rollingRadius = 0.34f;
  float wheelSlip = 0.0f;
  bool wheelDataValid = false;
};

void Reset();

// Modul cuma modulasi kalau telemetry CWheel valid. Kalau resolver gagal,
// pedal diserahin utuh ke ABS bawaan GTA.
float UpdateABS(Vehicle vehicle, VehicleData &data, float brakeInput,
                float speedMps);

void ToggleABS();
bool IsABSActive();
const State &GetState();

} // namespace BrakeSystem
