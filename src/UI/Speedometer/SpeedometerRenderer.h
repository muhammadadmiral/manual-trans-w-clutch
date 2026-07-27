#pragma once

#include <cstdint>

namespace Speedometer {

struct Data {
  float speedKmH = 0.0f;
  float normalizedRPM = 0.0f;
  float physicalRPM = 0.0f;
  // Zero means CVehicle only supplied normalized rev ratio.
  float redlineRPM = 0.0f;
  float fuel = 0.0f;
  float oilTemperature = 0.0f;
  float oilLife = 0.0f;
  float engineHealth = 0.0f;
  float gearboxHealth = 0.0f;
  float clutchHeat = 0.0f;
  float boost = 0.0f;
  float odometerKm = 0.0f;
  float throttle = 0.0f;
  float brake = 0.0f;
  float maximumSpeedKmH = 320.0f;
  int gear = 0;
  int maxGear = 0;
  int transmissionMode = 0;
  const char *automaticSelector = nullptr;
  bool motorcycle = false;
  bool electric = false;
  bool engineOn = false;
  bool engineStarting = false;
  bool parkingBrake = false;
  bool tcsEnabled = false;
  bool tcsActive = false;
  bool absEnabled = false;
  bool absActive = false;
  bool escEnabled = false;
  bool escActive = false;
  bool launchEnabled = false;
  bool rollWarning = false;
  bool launchControl = false;
  bool burnout = false;
  int vehicleClass = 0;
  std::uint32_t modelHash = 0;
};

void Draw(const Data &data);

} // namespace Speedometer
