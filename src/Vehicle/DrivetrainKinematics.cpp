#include "DrivetrainKinematics.h"

#include "VehicleData.h"
#include "VehicleProfile.h"
#include "../../sdk/inc/natives.h"

#include <algorithm>
#include <cmath>

namespace DrivetrainKinematics {
namespace {

float CurveExponent(VehicleProfile::Drivetrain profile) {
  switch (profile) {
  case VehicleProfile::Drivetrain::MotorcycleSequential:
  case VehicleProfile::Drivetrain::ScooterCVT:
    return 0.70f;
  case VehicleProfile::Drivetrain::UtilitySingleSpeed:
    return 0.82f;
  case VehicleProfile::Drivetrain::Electric:
    return 0.88f;
  default:
    // Enam gigi: G1 ~= 18% dan G2 ~= 35% dari top speed. Pada mobil
    // 240-250 km/h hasilnya sekitar 43-45 dan 84-89 km/h.
    return 0.96f;
  }
}

bool HasCoherentRatios(VehicleData &data, int maxGear) {
  if (maxGear < 1 || maxGear > 16)
    return false;

  float previous = 1000.0f;
  float first = 0.0f;
  float last = 0.0f;
  int valid = 0;
  int descending = 0;
  for (int gear = 1; gear <= maxGear; ++gear) {
    const float ratio =
        std::fabs(data.GetGearRatio(static_cast<uint8_t>(gear)));
    if (!std::isfinite(ratio) || ratio <= 0.03f || ratio > 20.0f)
      return false;
    if (gear == 1)
      first = ratio;
    if (gear > 1 && ratio < previous * 0.995f)
      ++descending;
    previous = ratio;
    last = ratio;
    ++valid;
  }

  if (valid != maxGear || first <= last * 1.35f)
    return maxGear == 1 && valid == 1;
  return descending >= (std::max)(1, maxGear - 2);
}

float AdaptiveLimit(Vehicle vehicle, int gear, int maxGear,
                    float estimatedTopSpeedMps) {
  const int absoluteGear = std::clamp(std::abs(gear), 1, (std::max)(1, maxGear));
  const float position =
      static_cast<float>(absoluteGear) /
      static_cast<float>((std::max)(1, maxGear));
  const float fraction =
      std::pow(position, CurveExponent(VehicleProfile::Detect(vehicle)));
  float limit = estimatedTopSpeedMps * fraction;
  if (gear < 0)
    limit *= 0.78f;
  return (std::max)(3.0f, limit);
}

} // namespace

Calibration Resolve(Vehicle vehicle, VehicleData &data, int gear, int maxGear) {
  Calibration result{};
  maxGear = std::clamp(maxGear, 1, 16);
  result.estimatedTopSpeedMps =
      (std::max)(8.0f, VEHICLE::GET_VEHICLE_ESTIMATED_MAX_SPEED(vehicle));
  result.ratioSetValid = HasCoherentRatios(data, maxGear);

  const int ratioIndex = gear < 0 ? 0 : std::clamp(gear, 1, maxGear);
  result.rawRatio =
      std::fabs(data.GetGearRatio(static_cast<uint8_t>(ratioIndex)));
  const float topRatio =
      std::fabs(data.GetGearRatio(static_cast<uint8_t>(maxGear)));

  const float memoryFlat = std::fabs(data.GetDriveMaxFlatVel());
  if (result.ratioSetValid && std::isfinite(memoryFlat) &&
      memoryFlat > 2.0f && memoryFlat < 250.0f && topRatio > 0.03f) {
    const float predictedTop = memoryFlat / topRatio;
    const float relative =
        predictedTop / (std::max)(1.0f, result.estimatedTopSpeedMps);
    result.memoryFlatVelocityValid =
        relative >= 0.68f && relative <= 1.42f;
  }

  result.flatVelocity =
      result.memoryFlatVelocityValid
          ? memoryFlat
          : result.estimatedTopSpeedMps * (topRatio > 0.03f ? topRatio
                                                            : 1.0f);

  const float adaptive =
      AdaptiveLimit(vehicle, gear, maxGear, result.estimatedTopSpeedMps);
  if (result.ratioSetValid && result.rawRatio > 0.03f) {
    const float ratioLimit = result.flatVelocity / result.rawRatio;
    // Handling yang sehat boleh mempertahankan karakter gearing kendaraan.
    // Hanya data ekstrim/korup yang diganti oleh kurva lintas kendaraan.
    const float relative = ratioLimit / adaptive;
    if (std::isfinite(ratioLimit) && ratioLimit > 2.0f &&
        relative >= 0.62f && relative <= 1.55f) {
      result.gearLimitSpeedMps = ratioLimit;
    } else {
      result.gearLimitSpeedMps = adaptive;
      result.usedAdaptiveCurve = true;
    }
  } else {
    result.gearLimitSpeedMps = adaptive;
    result.usedAdaptiveCurve = true;
  }

  return result;
}

float ResolveRoadRPM(Vehicle vehicle, VehicleData &data, int gear, int maxGear,
                     float speedMps) {
  if (gear == 0)
    return 0.0f;
  const Calibration calibration = Resolve(vehicle, data, gear, maxGear);
  if (calibration.gearLimitSpeedMps <= 0.1f)
    return 0.0f;
  return std::clamp(std::fabs(speedMps) / calibration.gearLimitSpeedMps,
                    0.0f, 1.25f);
}

} // namespace DrivetrainKinematics
