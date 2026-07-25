#include "EngineModel.h"
#include "../VehicleData.h"
#include "../VehicleUpgrades.h"
#include "../../Core/Config.h"
#include "../../Memory/GearboxPatches.h"
#include "../../../sdk/inc/natives.h"
#include <algorithm>
#include <cmath>

namespace EngineModel {

static State s_state;

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
};

static PhysicalRPMRange ResolvePhysicalRPMRange(Vehicle vehicle) {
  const int vehicleClass = VEHICLE::GET_VEHICLE_CLASS(vehicle);
  if (vehicleClass == 8)
    return {1200.0f, 10500.0f};
  if (vehicleClass == 6 || vehicleClass == 7)
    return {900.0f, 7800.0f};
  if (vehicleClass == 10 || vehicleClass == 11 ||
      vehicleClass == 12 || vehicleClass == 20)
    return {750.0f, 4800.0f};
  return {800.0f, 6800.0f};
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

static float ResolveFlatVelocity(Vehicle vehicle, VehicleData &data,
                                 int maxGear) {
  const float memoryValue = std::fabs(data.GetDriveMaxFlatVel());
  if (std::isfinite(memoryValue) && memoryValue > 1.0f)
    return memoryValue;

  const float estimatedTop =
      std::max(1.0f, VEHICLE::GET_VEHICLE_ESTIMATED_MAX_SPEED(vehicle));
  const float topRatio =
      maxGear > 0
          ? std::fabs(data.GetGearRatio(static_cast<uint8_t>(maxGear)))
          : 0.0f;
  return topRatio > 0.01f ? estimatedTop * topRatio : estimatedTop;
}

static float ResolveWheelRPM(Vehicle vehicle, VehicleData &data, int gear,
                             int maxGear, float speedMps, float idleRPM) {
  if (gear == 0)
    return idleRPM;
  const uint8_t ratioIndex =
      gear < 0 ? 0 : static_cast<uint8_t>(gear);
  const float ratio = std::fabs(data.GetGearRatio(ratioIndex));
  const float flatVelocity = ResolveFlatVelocity(vehicle, data, maxGear);
  if (ratio <= 0.01f || flatVelocity <= 1.0f)
    return idleRPM;
  return std::clamp(std::fabs(speedMps) * ratio / flatVelocity, 0.0f, 1.25f);
}

void Reset() {
  s_state = State{};
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
  const float maxFlatVel = ResolveFlatVelocity(vehicle, data, maxGear);
  const bool hasDrivelineData =
      std::isfinite(ratio) && ratio > 0.01f &&
      std::isfinite(maxFlatVel) && maxFlatVel > 1.0f;

  const float nativeTopSpeed =
      std::max(1.0f, VEHICLE::GET_VEHICLE_ESTIMATED_MAX_SPEED(vehicle));
  const float fallbackRatio =
      1.0f / static_cast<float>((std::max)(1, std::abs(gear)));
  const float effectiveRatio = hasDrivelineData ? ratio : fallbackRatio;
  const float effectiveTopSpeed = hasDrivelineData ? maxFlatVel : nativeTopSpeed;
  const float idleRoadSpeed =
      hasDrivelineData
          ? s_state.idleRPM * effectiveTopSpeed / effectiveRatio
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

  const PhysicalRPMRange rpmRange = ResolvePhysicalRPMRange(vehicle);
  s_state.estimatedIdlePhysicalRPM = rpmRange.idle;
  s_state.estimatedRedlineRPM = rpmRange.redline;
  const float engineSpeedForLoad =
      s_state.wheelRPM * Clamp01(engagement) +
      data.GetRPM() * (1.0f - Clamp01(engagement));
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

  s_state.creepThrottle = 0.0f;
  const float pitchLoad =
      Clamp01(std::max(0.0f, ENTITY::GET_ENTITY_PITCH(vehicle)) / 18.0f);
  const float idleHoldCapacity =
      Clamp01(Config::IdleTorqueFraction) * engagement *
      (std::abs(gear) == 1 ? 1.65f : 0.75f);
  s_state.hillRollback =
      std::abs(gear) == 1 && brake < 0.05f &&
      pitchLoad > idleHoldCapacity + Clamp01(throttle) * 0.45f;
  if (Config::IdleCreep && std::abs(gear) == 1 && throttle < 0.02f &&
      brake < 0.05f &&
      engagement > 0.50f && speedGap > 0.01f &&
      !s_state.hillRollback) {
    s_state.creepThrottle =
        Clamp01(Config::IdleCreepThrottle) * speedGap * engagement;
    const int driveControl = gear < 0 ? 72 : 71;
    PAD::DISABLE_CONTROL_ACTION(0, driveControl, true);
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(
        0, driveControl, s_state.creepThrottle);
  }

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

  const float stallClutch =
      std::clamp(Config::StallClutchThreshold, 0.30f, 0.95f);
  const float netReserve = s_state.torqueReserve - uphillLoad * 0.20f;
  const float decelerationRisk =
      Clamp01((-s_state.longitudinalAcceleration - 0.15f) / 2.50f);
  const bool unresolvedLug =
      netReserve < 0.0f ||
      (s_state.lugSeverity > 0.20f && decelerationRisk > 0.05f);
  const bool bogging = !automaticMode && Config::StallEnabled &&
                       engagement > stallClutch &&
                       s_state.estimatedEngineRPM < lugThreshold &&
                       unresolvedLug;
  if (bogging) {
    const float clutchLoad =
        Clamp01((engagement - stallClutch) / (1.0f - stallClutch));
    const float torqueDeficit = Clamp01(-netReserve / 0.65f);
    const float brakeLoad = 1.0f + Clamp01(brake) * 1.25f;
    const float highGearLoad =
        1.0f + gearWeight * 0.65f;
    const float unresolvedLoad =
        Clamp01(torqueDeficit + decelerationRisk * 0.55f);
    const float stallDelay =
        std::clamp(Config::LugStallDelay, 0.40f, 8.0f) *
        VehicleUpgrades::GetState().stallResistance *
        (0.55f + s_state.engineCondition * 0.45f);
    s_state.stallProgress +=
        clutchLoad * s_state.lugSeverity *
        (0.30f + unresolvedLoad * 1.70f) * brakeLoad * highGearLoad *
        dt * std::max(0.10f, Config::StallRate) / stallDelay;
  } else {
    s_state.stallProgress =
        std::max(0.0f, s_state.stallProgress -
                           dt * (engagement < stallClutch ? 3.5f : 1.25f));
  }

  if (s_state.stallProgress >= 1.0f) {
    s_state.stallProgress = 0.0f;
    return true;
  }

  const bool hardBraking =
      Config::HardBrakeStall && !automaticMode && engineOn && gear != 0 &&
      engagement > 0.75f && brake > 0.88f &&
      s_state.longitudinalAcceleration < -4.5f &&
      (std::fabs(speedMps) < 3.5f ||
       s_state.estimatedEngineRPM < lugThreshold * 0.85f);
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
                              float dt) {
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

  // RPM boleh mentok limiter, tapi gaya roda wajib habis setelah rasio gigi
  // mencapai redline. Ini yang mencegah gigi 1 narik tanpa batas.
  const float limiter =
      SmoothStep((s_state.wheelRPM - 1.0f) / 0.08f);
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
    s_state.inertia = inertia;
  } else {
    const float nativeAcceleration =
        std::max(0.01f, VEHICLE::GET_VEHICLE_ACCELERATION(vehicle));
    s_state.inertia =
        std::clamp(0.72f + nativeAcceleration * 1.10f, 0.65f, 1.60f);
  }

  // ScriptMain hanya memanggil model ini saat mode drivetrain aktif. Code
  // patch executable dikarantina di Enhanced, tetapi ownership RPM tetap
  // diperlukan untuk mengatasi throttle cut forced-gear native.
  const bool nativeOverride = true;
  const bool open =
      gear == 0 ||
      (automaticMode ? clutchEngagement < 0.08f
                     : clutchDisengagement > 0.35f);
  s_state.freeRevActive =
      engineOn && open && (!automaticMode || gear == 0);
  s_state.nativeCutRecovered = false;

  s_state.estimatedFlatVelocity =
      ResolveFlatVelocity(vehicle, data, maxGear);
  s_state.wheelRPM =
      ResolveWheelRPM(vehicle, data, gear, maxGear, speedMps,
                      s_state.idleRPM);
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

    const float pedal = Clamp01(throttle);
    const float freeTarget =
        s_state.idleRPM +
        std::pow(pedal, 0.62f) * (1.0f - s_state.idleRPM);
    float target = s_state.wheelRPM;

    if (automaticMode) {
      // Converter D boleh slip sedikit, tapi RPM utamanya tetap ikut rasio
      // dan kecepatan jalan. Hasilnya upshift menurunkan RPM, bukan rebound.
      const float converterSlip = 1.0f - Clamp01(clutchEngagement);
      target += converterSlip *
                (0.035f + std::pow(pedal, 0.70f) * 0.20f);
    } else if (clutchEngagement < 0.995f) {
      const float discSlip =
          std::pow(1.0f - Clamp01(clutchEngagement), 1.35f);
      target += (freeTarget - target) * discSlip;
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
                roadLoad * ratioLoad * 0.35f);
    s_state.engineBrake +=
        (target - s_state.engineBrake) * Clamp01(dt * 8.0f);
  } else {
    s_state.engineBrake +=
        (0.0f - s_state.engineBrake) * Clamp01(dt * 8.0f);
  }

  const bool stalled = UpdateLoadAndStall(
      vehicle, data, gear, maxGear, clutchEngagement, throttle, brake,
      speedMps, engineOn, dt, automaticMode);
  UpdateDriveTorque(gear, maxGear, clutchEngagement, throttle, brake,
                    engineOn, dt);
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
