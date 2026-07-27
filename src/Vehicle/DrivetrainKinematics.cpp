#include "DrivetrainKinematics.h"

#include "VehicleData.h"
#include "../../sdk/inc/natives.h"

#include <algorithm>
#include <cmath>

namespace DrivetrainKinematics {
namespace {

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

    const float initialHandlingFlat =
        std::fabs(data.GetInitialDriveMaxFlatVel());
    const float runtimeFlat = std::fabs(data.GetDriveMaxFlatVel());
    const bool initialHandlingFlatValid =
        std::isfinite(initialHandlingFlat) &&
        initialHandlingFlat > 2.0f && initialHandlingFlat < 250.0f;
    const float memoryFlat =
        initialHandlingFlatValid ? initialHandlingFlat : runtimeFlat;
    if (result.ratioSetValid && std::isfinite(memoryFlat) &&
        memoryFlat > 2.0f && memoryFlat < 250.0f && topRatio > 0.03f) {
        const float predictedTop = memoryFlat / topRatio;
        const float relative =
            predictedTop / (std::max)(1.0f, result.estimatedTopSpeedMps);
        result.memoryFlatVelocityValid =
            relative >= 0.68f && relative <= 1.42f;
    }

    result.flatVelocity =
        result.memoryFlatVelocityValid ? memoryFlat : 0.0f;

    if (result.memoryFlatVelocityValid && result.ratioSetValid &&
        result.rawRatio > 0.03f) {
        const float ratioLimit = result.flatVelocity / result.rawRatio;
        if (std::isfinite(ratioLimit) && ratioLimit > 2.0f) {
            result.gearLimitSpeedMps = ratioLimit;
        }
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
    return std::fabs(speedMps) / calibration.gearLimitSpeedMps;
}

} // namespace DrivetrainKinematics
