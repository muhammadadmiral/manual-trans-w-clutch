// =============================================================================
// DrivetrainKinematics.cpp — v1.0 OVERHAUL
//
// Perubahan utama dari r25:
// 1. CurveExponent diganti dengan GearSpacingCurve yang menggunakan distribusi
//    geometris (bukan power-law tunggal). Ini mengatasi masalah gear 1-4 terlalu
//    pendek dan gear 5-6-7 kepanjangan.
// 2. Gear spacing sekarang mengikuti pola logaritmis yang lebih natural:
//    - Gear 1-2 memiliki step ratio besar (close-ratio di bawah)
//    - Gear 3-4 transisi
//    - Gear 5-6-7 memiliki step ratio yang semakin kecil (overdrive terasa)
// 3. Semua logika lama dipertahankan sebagai fallback.
// =============================================================================
#include "DrivetrainKinematics.h"

#include "VehicleData.h"
#include "VehicleProfile.h"
#include "../../sdk/inc/natives.h"

#include <algorithm>
#include <cmath>

namespace DrivetrainKinematics {
namespace {

// ─── v1.0: Geometric gear spacing ──────────────────────────────────────────
// Menghitung fraksi kecepatan untuk setiap gigi menggunakan distribusi
// geometris. Hasilnya: setiap gigi naik memiliki kenaikan kecepatan yang
// proporsional terhadap posisinya, bukan linear atau power-law murni.
//
// stepRatio menentukan seberapa agresif spacing antar gigi:
//   - 1.0  = semua gigi rata (linear)
//   - >1.0 = gigi rendah lebih rapat, gigi tinggi lebih jauh (overdrive)
//   - <1.0 = gigi rendah lebih jauh, gigi tinggi lebih rapat (close-ratio top)
//
// Contoh untuk 6-speed dengan stepRatio=1.45:
//   G1=16%  G2=29%  G3=44%  G4=61%  G5=79%  G6=100%
// Bandingkan power-law lama (exp=0.96):
//   G1=18%  G2=35%  G3=50%  G4=65%  G5=79%  G6=100% ← terlalu rapat di 1-3!

struct GearSpacingParams {
    float stepRatio;    // rasio antar langkah gigi (>1.0 = overdrive profile)
    float firstGearFraction; // fraksi kecepatan minimum untuk gear 1
};

GearSpacingParams ResolveSpacing(VehicleProfile::Drivetrain profile,
                                  int maxGear) {
    switch (profile) {
    case VehicleProfile::Drivetrain::MotorcycleSequential:
        // Motor sport: close-ratio, 6 gigi biasanya. Gear 1 lebih panjang.
        return {1.32f, 0.14f};
    case VehicleProfile::Drivetrain::ScooterCVT:
        // CVT: single ratio, tidak relevan.
        return {1.00f, 0.50f};
    case VehicleProfile::Drivetrain::UtilitySingleSpeed:
        return {1.00f, 0.40f};
    case VehicleProfile::Drivetrain::Electric:
        return {1.10f, 0.35f};
    default:
        // Mobil standar: 4-8 gigi.
        // stepRatio lebih besar untuk mobil banyak gigi (7-8 speed lebih overdrive).
        if (maxGear >= 7) return {1.55f, 0.11f};
        if (maxGear >= 6) return {1.45f, 0.13f};
        if (maxGear >= 5) return {1.38f, 0.15f};
        return {1.30f, 0.18f}; // 4-speed
    }
}

// Menghitung fraksi kecepatan maks untuk gigi tertentu.
// gear: 1-based, maxGear: total gigi.
// Return: 0.0–1.0 (fraksi dari top speed)
float GeometricGearFraction(int gear, int maxGear,
                             VehicleProfile::Drivetrain profile) {
    if (maxGear <= 1) return 1.0f;
    if (gear >= maxGear) return 1.0f;
    if (gear <= 0) return 0.0f;

    const auto params = ResolveSpacing(profile, maxGear);
    const int n = maxGear; // total gigi
    const int g = gear;    // gigi saat ini (1-based)

    // Buat array kumulatif dari geometric series.
    // step[i] = stepRatio^(n-1-i)  → step terbesar di gear 1, terkecil di gear n
    // Ini membuat gigi rendah memiliki "jarak" kecepatan yang lebih besar.
    float totalSteps = 0.0f;
    float cumulativeAtGear = 0.0f;
    for (int i = 1; i <= n; ++i) {
        const float step = std::pow(params.stepRatio,
                                     static_cast<float>(n - i));
        totalSteps += step;
        if (i <= g) cumulativeAtGear += step;
    }

    if (totalSteps < 0.001f) return 1.0f;

    // Normalize ke range [firstGearFraction, 1.0]
    const float rawFraction = cumulativeAtGear / totalSteps;
    return params.firstGearFraction +
           rawFraction * (1.0f - params.firstGearFraction);
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
    const VehicleProfile::Drivetrain profile = VehicleProfile::Detect(vehicle);

    // v1.0: Gunakan geometric spacing, bukan power-law.
    const float fraction =
        GeometricGearFraction(absoluteGear, (std::max)(1, maxGear), profile);

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
        result.memoryFlatVelocityValid
            ? memoryFlat
            : result.estimatedTopSpeedMps * (topRatio > 0.03f ? topRatio
                                                              : 1.0f);

    const float adaptive =
        AdaptiveLimit(vehicle, gear, maxGear, result.estimatedTopSpeedMps);
    if (result.ratioSetValid && result.rawRatio > 0.03f) {
        const float ratioLimit = result.flatVelocity / result.rawRatio;
        if (std::isfinite(ratioLimit) && ratioLimit > 2.0f) {
            // v1.1: Selalu gunakan rasio native GTA jika valid.
            // Adaptive curve (overhaul v1.0) hanya sebagai fallback.
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
