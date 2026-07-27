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

// Mengamati hubungan rasio/kecepatan dari CTransmission dan handling.meta.
// Jika datanya tidak koheren, hasil dibiarkan invalid; tidak ada kurva kelas,
// perkiraan rasio, atau limiter kecepatan buatan.
Calibration Resolve(Vehicle vehicle, VehicleData &data, int gear, int maxGear);

float ResolveRoadRPM(Vehicle vehicle, VehicleData &data, int gear, int maxGear,
                     float speedMps);

} // namespace DrivetrainKinematics
