#pragma once

class VehicleData;
using Vehicle = int;

namespace DrivetrainKinematics {

struct Calibration {
  float estimatedTopSpeedMps = 0.0f;
  float flatVelocity = 0.0f;
  float gearLimitSpeedMps = 0.0f;
  float rawRatio = 0.0f;
  bool ratioSetValid = false;
  bool memoryFlatVelocityValid = false;
  bool usedAdaptiveCurve = false;
};

// Menghasilkan kecepatan limiter per gigi. Rasio handling tetap menjadi
// sumber utama, tetapi add-on vehicle dengan ratio/flat-velocity yang tidak
// koheren mendapat kurva fallback berdasarkan top speed native.
Calibration Resolve(Vehicle vehicle, VehicleData &data, int gear, int maxGear);

float ResolveRoadRPM(Vehicle vehicle, VehicleData &data, int gear, int maxGear,
                     float speedMps);

} // namespace DrivetrainKinematics
