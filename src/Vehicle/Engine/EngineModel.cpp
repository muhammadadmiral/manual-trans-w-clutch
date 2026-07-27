#include "EngineModel.h"
#include "../DrivetrainKinematics.h"
#include "../VehicleData.h"
#include "../VehicleUpgrades.h"
#include "../Maintenance/WorkshopTuning.h"
#include "../../Core/Config.h"
#include "../../Memory/GearboxPatches.h"
#include "../../Script/DrivingEventBus.h"
#include "../../../sdk/inc/natives.h"
#include <algorithm>
#include <cmath>

namespace EngineModel {

static State s_state;
static bool s_lugEventLatched = false;

static float Clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

static float SmoothStep(float value) {
  const float t = Clamp01(value);
  return t * t * (3.0f - 2.0f * t);
}

struct PhysicalRPMRange {
  float idle;
  float redline;
  float initialFlatVelocity;
  bool handlingBacked;
};

static PhysicalRPMRange ResolvePhysicalRPMRange(Vehicle vehicle,
                                                VehicleData &data) {
  // GTA exposes normalized RPM and the driveline velocity at normalized
  // redline, but not a physical redline-RPM field. Derive the display/load
  // scale from the vehicle's own handling instead of a vehicle-class table.
  const float initialFlat =
      std::fabs(data.GetInitialDriveMaxFlatVel());
  const bool handlingBacked =
      std::isfinite(initialFlat) && initialFlat > 2.0f &&
      initialFlat < 250.0f;
  const float nativeTopSpeed =
      std::max(8.0f, VEHICLE::GET_VEHICLE_ESTIMATED_MAX_SPEED(vehicle));
  const float drivelineVelocity =
      handlingBacked ? initialFlat : nativeTopSpeed * 0.85f;

  const float initialDriveForce = data.GetInitialDriveForce();
  const float finalReductionEstimate =
      std::clamp(3.50f +
                     (std::isfinite(initialDriveForce)
                          ? initialDriveForce
                          : 0.30f) *
                         3.0f,
                 3.50f, 5.40f);
  const float rollingRadius =
      std::clamp(s_state.rollingRadius, 0.24f, 0.55f);
  constexpr float kRadiansPerSecondToRPM =
      60.0f / (2.0f * 3.14159265358979323846f);
  const float redline =
      std::clamp(drivelineVelocity / rollingRadius *
                     finalReductionEstimate * kRadiansPerSecondToRPM,
                 3500.0f, 12000.0f);
  const float idle =
      std::clamp(redline *
                     std::clamp(s_state.idleRPM * 0.62f, 0.10f, 0.18f),
                 650.0f, 1600.0f);
  return {idle, redline, handlingBacked ? initialFlat : 0.0f,
          handlingBacked};
}

static float ToPhysicalRPM(float normalizedRPM, float idleNormalized,
                           const PhysicalRPMRange &range) {
  const float normalized = std::max(0.0f, normalizedRPM);
  const float idlePoint = std::clamp(idleNormalized, 0.08f, 0.35f);
  if (normalized <= idlePoint)
    return range.idle * normalized / idlePoint;
  const float aboveIdle =
      (normalized - idlePoint) / std::max(0.05f, 1.0f - idlePoint);
  return range.idle +
         aboveIdle * (range.redline - range.idle);
}

static float ResolveTorqueCurve(float engineRPM,
                                const PhysicalRPMRange &range) {
  if (engineRPM <= 0.0f)
    return 0.0f;
  if (engineRPM < range.idle) {
    const float subIdle = Clamp01(engineRPM / range.idle);
    return 0.18f + subIdle * 0.54f;
  }

  const float span = std::max(1000.0f, range.redline - range.idle);
  const float band = Clamp01((engineRPM - range.idle) / span);
  const float lowRise = SmoothStep(band / 0.30f);
  const float highFall = SmoothStep((band - 0.72f) / 0.28f);
  return std::clamp((0.72f + lowRise * 0.28f) *
                        (1.0f - highFall * 0.30f),
                    0.18f, 1.0f);
}

static float ResolveWheelRPM(Vehicle vehicle, VehicleData &data, int gear,
                             int maxGear, float speedMps, float throttle,
                             float brake, float engagement, float dt) {
  if (gear == 0) {
    s_state.wheelTelemetryValid = false;
    s_state.burnoutActive = false;
    s_state.drivenWheelSpeedMps = std::fabs(speedMps);
    return 0.0f;
  }

  const float bodySpeed = std::fabs(speedMps);
  const uint8_t count = data.GetWheelCount();
  float fastestDrivenOmega = 0.0f;
  float allOmega = 0.0f;
  int valid = 0;
  int driven = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const auto wheel = data.GetWheelTelemetry(i);
    if (!wheel.valid || !std::isfinite(wheel.angularVelocity))
      continue;
    const float omega = std::fabs(wheel.angularVelocity);
    allOmega += omega;
    ++valid;
    if (std::fabs(wheel.power) > 0.001f) {
      fastestDrivenOmega = std::max(fastestDrivenOmega, omega);
      ++driven;
    }
  }

  s_state.wheelTelemetryValid = valid >= 2;
  const float omega =
      driven > 0 ? fastestDrivenOmega
                 : (valid > 0 ? allOmega / static_cast<float>(valid) : 0.0f);
  if (s_state.wheelTelemetryValid && bodySpeed > 4.0f && omega > 1.0f &&
      throttle < 0.12f && brake < 0.08f) {
    const float measuredRadius = bodySpeed / omega;
    if (measuredRadius > 0.15f && measuredRadius < 0.80f) {
      s_state.rollingRadius +=
          (measuredRadius - s_state.rollingRadius) *
          Clamp01(dt * 1.8f);
    }
  }

  s_state.drivenWheelSpeedMps =
      s_state.wheelTelemetryValid ? omega * s_state.rollingRadius : bodySpeed;
  s_state.burnoutActive =
      gear == 1 && driven > 0 && engagement > 0.55f &&
      throttle > 0.35f && brake > 0.18f && bodySpeed < 6.0f &&
      s_state.drivenWheelSpeedMps > bodySpeed + 1.5f;
  const float effectiveSpeed =
      s_state.burnoutActive
          ? std::max(bodySpeed, s_state.drivenWheelSpeedMps)
          : bodySpeed;
  return DrivetrainKinematics::ResolveRoadRPM(
      vehicle, data, gear, maxGear, effectiveSpeed);
}

void Reset() {
  s_state = State{};
  s_lugEventLatched = false;
}

float PrepareIdleDrive(Vehicle vehicle, VehicleData &data, int gear,
                       int maxGear, float engagement, float throttle,
                       float brake, float speedMps, bool engineOn,
                       bool automaticMode, bool gentleClutchRelease) {
  s_state.creepThrottle = 0.0f;
  s_state.hillRollback = false;
  const float throttleGate = automaticMode ? 0.12f : 0.025f;

  // v1.0: Creep sekarang aktif di semua forward gear (bukan cuma gear 1).
  // Di gear tinggi, creep otomatis lemah karena speedGap kecil (mobil sudah jalan).
  // Gear 0 (Neutral) tetap tidak ada creep.
  if (!Config::IdleCreep || !engineOn || gear == 0 ||
      throttle >= throttleGate || engagement <= 0.08f)
    return 0.0f;

  const auto calibration =
      DrivetrainKinematics::Resolve(vehicle, data, gear, maxGear);
  const float gearLimitSpeed = calibration.gearLimitSpeedMps;
  const bool drivetrainEstimateValid =
      std::isfinite(gearLimitSpeed) && gearLimitSpeed > 1.0f;

  // v1.0: Creep target speed naik sedikit untuk automatic, memastikan mobil
  // benar-benar merayap maju saat brake dilepas di D.
  const float idleRoadSpeed =
      drivetrainEstimateValid
          ? std::clamp(
                s_state.idleRPM * gearLimitSpeed *
                    WorkshopTuning::GetCreepSpeedMultiplier(),
                0.0f, 5.5f)
          : (automaticMode ? 3.2f : 2.0f) *
                WorkshopTuning::GetCreepSpeedMultiplier();
  const float directionalSpeed = gear < 0 ? -speedMps : speedMps;
  const float speedGap =
      idleRoadSpeed > 0.01f
          ? Clamp01((idleRoadSpeed -
                     std::max(0.0f, directionalSpeed)) /
                    idleRoadSpeed)
          : 0.0f;
  if (speedGap <= 0.01f)
    return 0.0f;

  const float pitchLoad =
      Clamp01(std::max(0.0f, ENTITY::GET_ENTITY_PITCH(vehicle)) / 18.0f);
  const float idleHoldCapacity =
      Clamp01(Config::IdleTorqueFraction) * engagement * 1.65f;
  s_state.hillRollback =
      brake < 0.05f && pitchLoad > idleHoldCapacity;
  if (s_state.hillRollback)
    return 0.0f;

  const float base =
      Clamp01(Config::IdleCreepThrottle) *
      WorkshopTuning::GetCreepTorqueMultiplier();
  if (automaticMode) {
    // v1.0: Creep diperkuat. Brake release curve lebih agresif (0.18 bukan 0.25)
    // sehingga mobil mulai merayap lebih cepat saat brake diangkat.
    // Converter transfer di standstill diset lebih tinggi (0.82 base)
    // mensimulasikan torque converter stall ratio yang meneruskan torsi
    // bahkan saat turbine diam.
    const float brakeRelease =
        1.0f - SmoothStep(Clamp01(brake / 0.18f));
    const float pedalRelease =
        1.0f - SmoothStep(Clamp01(throttle / throttleGate));

    // v1.0: Torque converter transfer model yang lebih realistis.
    // Di standstill: stall ratio ~2.0x → transfer 0.82 base.
    // Saat mulai bergerak: coupling naik → transfer mendekati 1.0.
    const float standstillTransfer = 0.82f;
    const float movingTransfer = 0.95f;
    const float speedRatio = Clamp01(std::fabs(speedMps) / idleRoadSpeed);
    const float converterTransfer =
        standstillTransfer + speedRatio * (movingTransfer - standstillTransfer);

    // v1.0: Base creep throttle minimum 0.14 (naik dari 0.075) agar
    // cukup kuat mendorong mobil dari diam.
    s_state.creepThrottle =
        std::max(0.14f, base * 1.5f) * speedGap * brakeRelease *
        converterTransfer * pedalRelease;
  } else if (gentleClutchRelease && brake < 0.05f) {
    // Manual mode: creep halus saat kopling dilepas perlahan di gear rendah.
    const float biteTransfer =
        SmoothStep((Clamp01(engagement) - 0.12f) / 0.78f);
    const float idleGovernor =
        0.10f * biteTransfer * speedGap;
    s_state.creepThrottle =
        (base + idleGovernor) * speedGap * biteTransfer;
  }

  // v1.0: Max creep throttle sedikit dinaikkan (0.38 dari 0.32) agar cukup
  // kuat untuk menjaga kendaraan merayap di tanjakan ringan.
  s_state.creepThrottle =
      std::clamp(s_state.creepThrottle, 0.0f, 0.38f);
  return s_state.creepThrottle;
}

static bool UpdateEnvironment(Vehicle vehicle, bool engineOn, float dt) {
  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
  const bool electric = VEHICLE::_GET_IS_VEHICLE_ELECTRIC(model) != FALSE;
  const bool motorcycle =
      VEHICLE::IS_THIS_MODEL_A_BIKE(model) ||
      VEHICLE::IS_THIS_MODEL_A_QUADBIKE(model);
  s_state.airborne = ENTITY::IS_ENTITY_IN_AIR(vehicle) != FALSE;
  s_state.upsideDown =
      ENTITY::IS_ENTITY_UPSIDEDOWN(vehicle) != FALSE ||
      VEHICLE::IS_VEHICLE_STUCK_ON_ROOF(vehicle) != FALSE;
  s_state.environmentStall = false;

  if (!engineOn || electric) {
    s_state.waterIngestion =
        std::max(0.0f, s_state.waterIngestion - dt * 2.0f);
    s_state.oilStarvation =
        std::max(0.0f, s_state.oilStarvation - dt * 2.0f);
    return false;
  }

  const float submerged =
      std::clamp(ENTITY::GET_ENTITY_SUBMERGED_LEVEL(vehicle), 0.0f, 1.0f);
  if (submerged > 0.58f) {
    const float waterDelay =
        std::clamp(Config::WaterStallDelay, 0.50f, 12.0f) *
        (motorcycle ? 0.65f : 1.0f);
    s_state.waterIngestion +=
        dt * (0.35f + submerged * 0.90f) / waterDelay;
  } else {
    s_state.waterIngestion =
        std::max(0.0f, s_state.waterIngestion - dt * 0.75f);
  }

  if (s_state.upsideDown) {
    const float rolloverDelay =
        std::clamp(Config::RolloverStallDelay, 1.0f, 20.0f) *
        (motorcycle ? 0.24f : 1.0f);
    s_state.oilStarvation += dt / rolloverDelay;
  } else {
    s_state.oilStarvation =
        std::max(0.0f, s_state.oilStarvation - dt * 0.45f);
  }

  if (s_state.waterIngestion >= 1.0f ||
      s_state.oilStarvation >= 1.0f) {
    s_state.environmentStall = true;
    s_state.waterIngestion = std::min(s_state.waterIngestion, 1.0f);
    s_state.oilStarvation = std::min(s_state.oilStarvation, 1.0f);
    return true;
  }
  return false;
}

static bool UpdateLoadAndStall(Vehicle vehicle, VehicleData &data, int gear,
                               int maxGear,
                               float engagement, float throttle,
                               float brake, float speedMps, bool engineOn, float dt,
                               bool automaticMode) {
  if (!engineOn || gear == 0) {
    s_state.load = 0.0f;
    s_state.creepThrottle = 0.0f;
    s_state.torqueReserve = 0.0f;
    s_state.lugSeverity = 0.0f;
    s_state.stallProgress =
        std::max(0.0f, s_state.stallProgress - dt * 3.0f);
    return false;
  }

  const uint8_t ratioIndex =
      gear < 0 ? 0 : static_cast<uint8_t>(gear);
  const float ratio = std::fabs(data.GetGearRatio(ratioIndex));
  const auto calibration =
      DrivetrainKinematics::Resolve(vehicle, data, gear, maxGear);
  const float maxFlatVel = calibration.flatVelocity;
  const bool hasDrivelineData =
      std::isfinite(calibration.gearLimitSpeedMps) &&
      calibration.gearLimitSpeedMps > 1.0f;

  const float nativeTopSpeed =
      std::max(1.0f, VEHICLE::GET_VEHICLE_ESTIMATED_MAX_SPEED(vehicle));
  const float fallbackRatio =
      1.0f / static_cast<float>((std::max)(1, std::abs(gear)));
  const float effectiveRatio = hasDrivelineData ? ratio : fallbackRatio;
  const float effectiveTopSpeed = hasDrivelineData ? maxFlatVel : nativeTopSpeed;
  const float idleRoadSpeed =
      hasDrivelineData
          ? s_state.idleRPM * calibration.gearLimitSpeedMps
          : s_state.idleRPM * effectiveTopSpeed *
                static_cast<float>((std::max)(1, std::abs(gear))) /
                static_cast<float>((std::max)(1, maxGear));
  const float directionalSpeed =
      gear < 0 ? -speedMps : speedMps;
  const float usefulSpeed = std::max(0.0f, directionalSpeed);
  const float speedGap =
      idleRoadSpeed > 0.01f
          ? Clamp01((idleRoadSpeed - usefulSpeed) / idleRoadSpeed)
          : 0.0f;
  s_state.load = engagement * speedGap;

  const PhysicalRPMRange rpmRange =
      ResolvePhysicalRPMRange(vehicle, data);
  s_state.estimatedIdlePhysicalRPM = rpmRange.idle;
  s_state.estimatedRedlineRPM = rpmRange.redline;
  s_state.initialDriveMaxFlatVel = rpmRange.initialFlatVelocity;
  s_state.redlineHandlingBacked = rpmRange.handlingBacked;
  // Torque converter memindahkan torsi tanpa menyamakan putaran poros.
  // Mencampur RPM roda di sini bikin mesin matic terbaca 300-400 RPM saat
  // diam, lalu model torsinya sendiri menganggap mesin lug parah.
  // Stall harus mengikuti RPM mesin yang benar-benar ditampilkan/dikontrol.
  // Road RPM tetap dipakai untuk load dan limiter, tetapi tidak boleh
  // menyatakan mesin 0 RPM hanya karena body diam saat roda sedang burnout.
  const float engineSpeedForLoad =
      s_state.rpmOwned ? s_state.controlledRPM : data.GetRPM();
  s_state.estimatedEngineRPM =
      ToPhysicalRPM(engineSpeedForLoad, s_state.idleRPM, rpmRange);
  const float lugThreshold =
      std::clamp(Config::LugStallRPM, rpmRange.idle + 100.0f,
                 rpmRange.redline * 0.45f);
  s_state.lugSeverity =
      Clamp01((lugThreshold - s_state.estimatedEngineRPM) /
              std::max(250.0f, lugThreshold - rpmRange.idle * 0.55f));
  s_state.torqueCurve =
      ResolveTorqueCurve(s_state.estimatedEngineRPM, rpmRange);

  const float pitchLoad =
      Clamp01(std::max(0.0f, ENTITY::GET_ENTITY_PITCH(vehicle)) / 18.0f);

  const float firstRatio = std::fabs(data.GetGearRatio(1));
  const float gearLeverage =
      firstRatio > 0.01f && hasDrivelineData
          ? Clamp01(effectiveRatio / firstRatio)
          : fallbackRatio;
  const float driveForce = data.GetDriveForce();
  const float nativeAcceleration =
      std::max(0.01f, VEHICLE::GET_VEHICLE_ACCELERATION(vehicle));
  const float driveScale =
      std::isfinite(driveForce) && driveForce > 0.01f
          ? std::clamp(driveForce / 0.30f, 0.35f, 2.0f)
          : std::clamp(nativeAcceleration / 0.30f, 0.45f, 1.80f);
  const float effectiveThrottle =
      std::max(Clamp01(throttle), s_state.creepThrottle);
  const float engineHealth =
      VEHICLE::GET_VEHICLE_ENGINE_HEALTH(vehicle);
  s_state.engineCondition =
      std::clamp(engineHealth / 1000.0f, 0.20f, 1.0f);
  const float conditionTorque =
      0.45f + s_state.engineCondition * 0.55f;
  const float idleTorque =
      std::clamp(Config::IdleTorqueFraction +
                     VehicleUpgrades::GetState().engineStage * 0.035f,
                 0.02f, 0.65f);
  const float availableTorque =
      (idleTorque + effectiveThrottle * (1.0f - idleTorque)) *
      s_state.torqueCurve * conditionTorque * driveScale * gearLeverage;
  const float gearSpan =
      static_cast<float>((std::max)(1, maxGear - 1));
  const float gearWeight =
      Clamp01(static_cast<float>((std::max)(0, std::abs(gear) - 1)) /
              gearSpan);
  const float uphillLoad = pitchLoad;
  const float launchDemand =
      speedGap * (0.55f + gearWeight * 0.20f);
  const float lugDemand =
      s_state.lugSeverity * (0.06f + gearWeight * 0.12f);
  const float externalLoad =
      uphillLoad * 0.35f + Clamp01(brake) * 0.75f;
  const float torqueDemand =
      engagement * (launchDemand + lugDemand + externalLoad);
  s_state.torqueReserve = availableTorque - torqueDemand;

  // v1.1: Stall disederhanakan. Jika RPM fisik mesin jatuh di bawah
  // batas idle bawaan kendaraan (650-900 RPM tergantung mobil), mesin
  // langsung mati. Automatic mode tidak stall (torque converter).
  const float stallClutch =
      std::clamp(Config::StallClutchThreshold, 0.30f, 0.95f);
  if (!automaticMode && Config::StallEnabled &&
      engagement > stallClutch &&
      s_state.estimatedEngineRPM < rpmRange.idle &&
      !s_state.burnoutActive) {
    s_state.stallProgress = 0.0f;
    return true;
  } else {
    s_state.stallProgress =
        std::max(0.0f, s_state.stallProgress -
                           dt * (engagement < stallClutch ? 3.5f : 1.25f));
  }

  const bool hardBraking =
      Config::HardBrakeStall && !automaticMode && engineOn && gear != 0 &&
      engagement > 0.75f && brake > 0.88f &&
      throttle < 0.12f && !s_state.burnoutActive &&
      s_state.longitudinalAcceleration < -4.5f &&
      (std::fabs(speedMps) < 3.5f ||
       s_state.estimatedEngineRPM < rpmRange.idle);
  if (hardBraking) {
    s_state.hardBrakeStallProgress += dt / 0.22f;
  } else {
    s_state.hardBrakeStallProgress =
        std::max(0.0f, s_state.hardBrakeStallProgress - dt * 4.0f);
  }
  s_state.hardBrakeStall = s_state.hardBrakeStallProgress >= 1.0f;
  if (s_state.hardBrakeStall) {
    s_state.hardBrakeStallProgress = 0.0f;
    return true;
  }
  return false;
}

static void UpdateDriveTorque(int gear, int maxGear, float engagement,
                              float throttle, float brake, bool engineOn,
                              bool automaticMode, float dt) {
  s_state.driveTorqueFactor = 1.0f;
  s_state.redlineCut = false;
  if (!engineOn || gear == 0 || engagement < 0.08f ||
      s_state.airborne || s_state.upsideDown) {
    s_state.lowRpmRecovery +=
        (0.0f - s_state.lowRpmRecovery) * Clamp01(dt * 6.0f);
    return;
  }

  const int absoluteGear = std::abs(gear);
  const float gearSpan =
      static_cast<float>((std::max)(1, maxGear - 1));
  const float gearWeight =
      Clamp01(static_cast<float>((std::max)(0, absoluteGear - 1)) / gearSpan);
  const float rpmBand =
      Clamp01((s_state.wheelRPM - s_state.idleRPM) /
              std::max(0.05f, 0.52f - s_state.idleRPM));
  const float lowRpmDemand = 1.0f - SmoothStep(rpmBand);
  const float positiveReserve =
      Clamp01(s_state.torqueReserve / 0.55f);
  const float torqueDeficit =
      Clamp01(-s_state.torqueReserve / 0.65f);

  // Native GTA terlalu gampang kehilangan drive di gigi tinggi bawah.
  // Tambahan ini tetap lewat torque multiplier kendaraan dan rasio native;
  // velocity kendaraan sama sekali nggak disentuh.
  const float lowRpmAssist =
      Clamp01(throttle) * lowRpmDemand * positiveReserve *
      (0.18f + gearWeight * 0.62f);

  // Kalau GTA masih ngerem forced gear walau cadangan torsinya positif,
  // naikkan multiplier secara bertahap. Ini tetap gaya drivetrain native:
  // posisi dan kecepatan kendaraan nggak pernah ditulis.
  const float requestedAcceleration =
      Clamp01(throttle) * (0.25f + positiveReserve * 1.10f);
  const float accelerationDeficit =
      Clamp01((requestedAcceleration - s_state.longitudinalAcceleration) /
              3.25f);
  const bool needsRecovery =
      throttle > 0.12f && brake < 0.10f && engagement > 0.62f &&
      positiveReserve > 0.02f && lowRpmDemand > 0.02f;
  const float recoveryTarget =
      needsRecovery
          ? Clamp01(throttle) * lowRpmDemand *
                (0.45f + positiveReserve * 0.55f) *
                (0.65f + gearWeight * 2.10f) * accelerationDeficit
          : 0.0f;
  const float recoveryRate =
      recoveryTarget > s_state.lowRpmRecovery ? 2.8f : 7.0f;
  s_state.lowRpmRecovery +=
      (recoveryTarget - s_state.lowRpmRecovery) *
      Clamp01(dt * recoveryRate);

  // Defisit torsi bikin mesin berat, bukan mematikan throttle seketika.
  // Stall timer yang memutus mesin kalau kondisi ini dibiarkan.
  const float lugFactor =
      1.0f - torqueDeficit * (0.32f + (1.0f - Clamp01(throttle)) * 0.18f);
  float output =
      (1.0f + lowRpmAssist + s_state.lowRpmRecovery) * lugFactor;

  if (automaticMode && gear > 0) {
    const float converterSlip = 1.0f - Clamp01(engagement);
    const float launchBand =
        1.0f - SmoothStep(s_state.wheelRPM / 0.42f);
    const float converterMultiplication =
        1.0f + converterSlip * launchBand * Clamp01(throttle) * 0.90f;
    output *= converterMultiplication;
  }

  // RPM boleh mentok limiter, tapi gaya roda wajib habis setelah rasio gigi
  // mencapai redline. Ini yang mencegah gigi 1 narik tanpa batas.
  const float ratioLimiter =
      SmoothStep((s_state.wheelRPM - 1.0f) / 0.08f);
  const float engineLimiter =
      SmoothStep((s_state.controlledRPM - 0.965f) / 0.035f);
  const float limiter = ratioLimiter * engineLimiter;
  output *= 1.0f - limiter;
  s_state.redlineCut = limiter >= 0.98f;
  s_state.driveTorqueFactor = std::clamp(output, 0.0f, 4.50f);
}

bool Update(Vehicle vehicle, VehicleData &data, int gear, int maxGear,
            float clutchDisengagement, float clutchEngagement,
            float throttle, float brake, float speedMps, bool engineOn,
            bool automaticMode) {
  const float dt = std::clamp(MISC::GET_FRAME_TIME(), 0.001f, 0.05f);
  const float rpm = data.GetRPM();
  const bool environmentStall =
      UpdateEnvironment(vehicle, engineOn, dt);
  const float directionalSpeed = gear < 0 ? -speedMps : speedMps;
  if (!engineOn || gear == 0) {
    s_state.speedSampleValid = false;
    s_state.longitudinalAcceleration +=
        (0.0f - s_state.longitudinalAcceleration) * Clamp01(dt * 8.0f);
  } else if (!s_state.speedSampleValid) {
    s_state.previousDirectionalSpeed = directionalSpeed;
    s_state.longitudinalAcceleration = 0.0f;
    s_state.speedSampleValid = true;
  } else {
    const float rawAcceleration =
        std::clamp((directionalSpeed - s_state.previousDirectionalSpeed) / dt,
                   -12.0f, 12.0f);
    const float accelAlpha = 1.0f - std::exp(-5.0f * dt);
    s_state.longitudinalAcceleration +=
        (rawAcceleration - s_state.longitudinalAcceleration) * accelAlpha;
    s_state.previousDirectionalSpeed = directionalSpeed;
  }

  if (engineOn && throttle < 0.01f && rpm > 0.05f && rpm < 0.35f)
    s_state.idleRPM += (rpm - s_state.idleRPM) * Clamp01(dt * 2.0f);

  const float inertia = data.GetDriveInertia();
  s_state.handlingBacked =
      std::isfinite(inertia) && inertia >= 0.05f && inertia <= 10.0f;
  if (s_state.handlingBacked) {
    s_state.inertia =
        inertia * WorkshopTuning::GetFlywheelInertiaMultiplier();
  } else {
    const float nativeAcceleration =
        std::max(0.01f, VEHICLE::GET_VEHICLE_ACCELERATION(vehicle));
    s_state.inertia =
        std::clamp(0.72f + nativeAcceleration * 1.10f, 0.65f, 1.60f) *
        WorkshopTuning::GetFlywheelInertiaMultiplier();
  }

  // ScriptMain hanya memanggil model ini saat mode drivetrain aktif. Ownership
  // RPM tetap jalan saat override native sengaja dimatikan lewat config.
  const bool nativeOverride = true;
  const bool open =
      gear == 0 ||
      (automaticMode ? clutchEngagement < 0.08f
                     : clutchDisengagement > 0.35f);
  s_state.freeRevActive =
      engineOn && open && (!automaticMode || gear == 0);
  s_state.nativeCutRecovered = false;

  const auto activeCalibration =
      DrivetrainKinematics::Resolve(vehicle, data, gear, maxGear);
  s_state.estimatedFlatVelocity = activeCalibration.flatVelocity;
  s_state.gearLimitSpeedMps = activeCalibration.gearLimitSpeedMps;
  s_state.adaptiveGearing = activeCalibration.usedAdaptiveCurve;
  s_state.wheelRPM =
      ResolveWheelRPM(vehicle, data, gear, maxGear, speedMps, throttle,
                      brake, clutchEngagement, dt);
  s_state.connectedRPMTarget = s_state.wheelRPM;
  s_state.expectedRPM = open ? rpm : std::min(1.0f, s_state.wheelRPM);

  if (!engineOn) {
    s_state.rpmOwned = false;
    s_state.controlledRPM = 0.0f;
  } else if (open) {
    if (!s_state.rpmOwned) {
      s_state.controlledRPM =
          std::clamp(std::max(rpm, s_state.idleRPM), s_state.idleRPM, 1.0f);
      s_state.rpmOwned = true;
    }

    // GTA memutus tenaga di field clutch, tapi tidak free-rev konsisten di
    // gear 2+. Di fase terbuka saja kita lanjutkan state RPM mesin memakai
    // inertia kendaraan; kecepatan roda tidak pernah ditulis.
    const float pedal = Clamp01(throttle);
    float freeTarget =
        s_state.idleRPM +
        std::pow(pedal, 0.62f) * (1.0f - s_state.idleRPM);
    if (s_state.previousThrottle > 0.15f && pedal < 0.02f)
      s_state.revHangRemaining =
          std::clamp(Config::RevHangDuration, 0.0f, 2.0f);
    if (s_state.revHangRemaining > 0.0f && pedal < 0.02f) {
      s_state.revHangRemaining =
          std::max(0.0f, s_state.revHangRemaining - dt);
      freeTarget = std::max(freeTarget, s_state.controlledRPM);
    }
    const float clutchDrag =
        gear == 0 ? 0.0f : Clamp01(clutchEngagement * clutchEngagement * 0.65f);
    const float target =
        freeTarget + (s_state.wheelRPM - freeTarget) * clutchDrag;
    s_state.connectedRPMTarget = target;
    const float inertiaScale = std::clamp(s_state.inertia, 0.30f, 3.0f);
    const float response =
        target >= s_state.controlledRPM
            ? 1.45f + inertiaScale * 1.65f
            : 0.85f + 1.15f / std::sqrt(inertiaScale);
    const float alpha = 1.0f - std::exp(-response * dt);

    if (pedal > 0.01f && rpm > s_state.controlledRPM)
      s_state.controlledRPM = std::min(rpm, 1.0f);
    s_state.controlledRPM +=
        (target - s_state.controlledRPM) * Clamp01(alpha);
    s_state.controlledRPM =
        std::clamp(s_state.controlledRPM, s_state.idleRPM, 1.0f);

    data.SetRPM(s_state.controlledRPM);
    data.SetThrottle(pedal);
    data.SetThrottlePedal(pedal);
    s_state.expectedRPM = s_state.controlledRPM;
  } else if (nativeOverride && gear > 0 && !s_state.airborne &&
             !s_state.upsideDown) {
    if (!s_state.rpmOwned) {
      s_state.controlledRPM =
          std::clamp(std::max(rpm, s_state.idleRPM), s_state.idleRPM, 1.0f);
      s_state.rpmOwned = true;
    }

    // v1.1: Sinkronisasi dengan RPM yang sudah diset oleh AutomaticGearbox.
    // Jika RPM aktual di memori berbeda jauh dari controlledRPM (karena
    // AutomaticGearbox::ApplyToMemory memaksa nilai saat shifting), snap
    // controlledRPM ke nilai tersebut agar tidak terjadi "fighting".
    if (automaticMode && std::fabs(rpm - s_state.controlledRPM) > 0.05f) {
      s_state.controlledRPM = rpm;
    }

    const float pedal = Clamp01(throttle);
    const float freeTarget =
        s_state.idleRPM +
        std::pow(pedal, 0.62f) * (1.0f - s_state.idleRPM);
    float target = s_state.wheelRPM;
    const bool powerBrakeWindow =
        !automaticMode && gear == 1 && std::fabs(speedMps) < 3.0f &&
        pedal > 0.35f && brake > 0.18f;

    if (automaticMode) {
      // Converter D boleh slip sedikit, tapi RPM utamanya tetap ikut rasio
      // dan kecepatan jalan. Hasilnya upshift menurunkan RPM, bukan rebound.
      const float converterSlip = 1.0f - Clamp01(clutchEngagement);
      const float stallRise =
          0.04f + std::pow(pedal, 0.70f) * 0.24f;
      target =
          std::max(target, s_state.idleRPM + converterSlip * stallRise);
    } else if (clutchEngagement < 0.995f) {
      const float discSlip =
          std::pow(1.0f - Clamp01(clutchEngagement), 1.35f);
      target += (freeTarget - target) * discSlip;
    } else if (powerBrakeWindow && !s_state.burnoutActive) {
      // Sedikit torsional slip sebelum ban mulai berputar. Begitu wheel
      // telemetry membaca wheelspin, target penuh kembali berasal dari roda.
      target = std::max(
          target, s_state.idleRPM + std::pow(pedal, 0.75f) * 0.10f);
    }

    target = std::clamp(target, s_state.idleRPM, 1.08f);
    s_state.connectedRPMTarget = target;

    const float inertiaScale = std::clamp(s_state.inertia, 0.30f, 3.0f);
    const float response =
        automaticMode ? 7.0f / std::sqrt(inertiaScale)
                      : 11.0f / std::sqrt(inertiaScale);
    const float alpha = 1.0f - std::exp(-response * dt);
    s_state.controlledRPM +=
        (target - s_state.controlledRPM) * Clamp01(alpha);
    s_state.controlledRPM =
        std::clamp(s_state.controlledRPM, s_state.idleRPM, 1.08f);

    const float nativeThrottle = data.GetThrottle();
    s_state.nativeCutRecovered =
        (pedal > 0.10f && nativeThrottle < pedal * 0.25f) ||
        (s_state.wheelRPM > s_state.idleRPM + 0.06f &&
         rpm + 0.08f < s_state.wheelRPM);
    data.SetRPM(s_state.controlledRPM);
    data.SetThrottle(pedal);
    data.SetThrottlePedal(pedal);
    s_state.expectedRPM = s_state.controlledRPM;
  } else {
    s_state.rpmOwned = false;
    s_state.controlledRPM = rpm;
  }
  s_state.previousRPM = rpm;
  s_state.previousThrottle = Clamp01(throttle);

  const float ratio =
      gear > 0 ? std::fabs(data.GetGearRatio(static_cast<uint8_t>(gear)))
               : 0.0f;
  const float top =
      maxGear > 0
          ? std::fabs(data.GetGearRatio(static_cast<uint8_t>(maxGear)))
          : 0.0f;
  if (engineOn && gear > 0 && clutchEngagement > 0.6f &&
      throttle < 0.05f) {
    const float nativeTop =
        std::max(1.0f, VEHICLE::GET_VEHICLE_ESTIMATED_MAX_SPEED(vehicle));
    const float ratioLoad =
        ratio > 0.01f && top > 0.01f
            ? Clamp01(ratio / top / static_cast<float>((std::max)(1, maxGear)))
            : Clamp01(static_cast<float>((std::max)(1, maxGear)) /
                      static_cast<float>((std::max)(1, gear)) / 3.0f);
    const float rpmOverrun =
        Clamp01((rpm - s_state.idleRPM) /
                std::max(0.05f, 1.0f - s_state.idleRPM));
    const float roadLoad =
        Clamp01(std::fabs(speedMps) / nativeTop);
    const float target =
        Clamp01(rpmOverrun * (0.45f + ratioLoad * 0.55f) +
                roadLoad * ratioLoad * 0.35f) *
        WorkshopTuning::GetEngineBrakeMultiplier();
    s_state.engineBrake +=
        (target - s_state.engineBrake) * Clamp01(dt * 8.0f);
  } else {
    s_state.engineBrake +=
        (0.0f - s_state.engineBrake) * Clamp01(dt * 8.0f);
  }

  const bool stalled = UpdateLoadAndStall(
      vehicle, data, gear, maxGear, clutchEngagement, throttle, brake,
      speedMps, engineOn, dt, automaticMode);
  if (engineOn && gear != 0 && s_state.lugSeverity > 0.58f &&
      s_state.load > 0.28f && !s_lugEventLatched) {
    DrivingEventBus::EventData event{};
    event.vehicle = vehicle;
    event.severity = s_state.lugSeverity;
    event.value = s_state.estimatedEngineRPM;
    DrivingEventBus::Publish(
        DrivingEventBus::Event::EngineLug, event);
    s_lugEventLatched = true;
  } else if (s_state.lugSeverity < 0.32f || !engineOn) {
    s_lugEventLatched = false;
  }
  UpdateDriveTorque(gear, maxGear, clutchEngagement, throttle, brake,
                    engineOn, automaticMode, dt);
  return stalled || environmentStall;
}

float GetLoad() { return s_state.load; }
float GetStallProgress() { return s_state.stallProgress; }
float GetEngineBrake() { return s_state.engineBrake; }
float GetInertia() { return s_state.inertia; }
float GetExpectedRPM() { return s_state.expectedRPM; }
float GetCreepThrottle() { return s_state.creepThrottle; }
float GetTorqueReserve() { return s_state.torqueReserve; }
float GetDriveTorqueFactor() { return s_state.driveTorqueFactor; }
const State &GetState() { return s_state; }

} // namespace EngineModel
