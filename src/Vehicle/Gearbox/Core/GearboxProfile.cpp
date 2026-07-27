#include "GearboxProfile.h"

#include "../../VehicleData.h"
#include "../../VehicleProfile.h"
#include "../../VehicleUpgrades.h"
#include "../../../Core/ModLogger.h"
#include "../../../../sdk/inc/natives.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace GearboxProfile {
namespace {

State s_state;
char s_iniPath[MAX_PATH]{};

float ClampRatio(float value, float fallback, bool reverse) {
  if (!std::isfinite(value))
    return fallback;
  if (reverse) {
    if (value > -0.05f || value < -20.0f)
      return fallback;
  } else if (value < 0.05f || value > 20.0f) {
    return fallback;
  }
  return value;
}

float ReadFloat(const char *section, const char *key, float fallback) {
  if (!s_iniPath[0])
    return fallback;
  char text[48]{};
  if (!GetPrivateProfileStringA(
          section, key, "", text, sizeof(text), s_iniPath))
    return fallback;
  char *end = nullptr;
  const float value = std::strtof(text, &end);
  return end == text || !std::isfinite(value) ? fallback : value;
}

bool ReadBool(const char *section, const char *key, bool fallback) {
  if (!s_iniPath[0])
    return fallback;
  return GetPrivateProfileIntA(
             section, key, fallback ? 1 : 0, s_iniPath) != 0;
}

Architecture ParseArchitecture(const char *section,
                               Architecture fallback) {
  char text[48]{};
  if (!s_iniPath[0] ||
      !GetPrivateProfileStringA(
          section, "Type", "", text, sizeof(text), s_iniPath))
    return fallback;
  for (char *cursor = text; *cursor; ++cursor)
    if (*cursor >= 'a' && *cursor <= 'z')
      *cursor = static_cast<char>(*cursor - ('a' - 'A'));

  if (!std::strcmp(text, "MANUAL") || !std::strcmp(text, "SYNCHROMESH"))
    return Architecture::SynchromeshManual;
  if (!std::strcmp(text, "DOGBOX"))
    return Architecture::DogBox;
  if (!std::strcmp(text, "SEQUENTIAL"))
    return Architecture::Sequential;
  if (!std::strcmp(text, "AUTOMATIC") ||
      !std::strcmp(text, "TORQUE_CONVERTER"))
    return Architecture::TorqueConverter;
  if (!std::strcmp(text, "DCT") || !std::strcmp(text, "DUAL_CLUTCH"))
    return Architecture::DualClutch;
  if (!std::strcmp(text, "CVT"))
    return Architecture::CVT;
  if (!std::strcmp(text, "SINGLE_SPEED"))
    return Architecture::SingleSpeed;
  return Architecture::HandlingNative;
}

Architecture DeriveArchitecture(Vehicle vehicle,
                                VehicleProfile::Drivetrain drivetrain) {
  switch (drivetrain) {
  case VehicleProfile::Drivetrain::ScooterCVT:
    return Architecture::CVT;
  case VehicleProfile::Drivetrain::UtilitySingleSpeed:
  case VehicleProfile::Drivetrain::Electric:
    return Architecture::SingleSpeed;
  case VehicleProfile::Drivetrain::MotorcycleSequential:
    return Architecture::Sequential;
  default:
    break;
  }

  const auto &upgrades = VehicleUpgrades::GetState();
  if (upgrades.raceTransmission)
    return upgrades.quickshifter ? Architecture::DualClutch
                                 : Architecture::DogBox;
  const int vehicleClass = VEHICLE::GET_VEHICLE_CLASS(vehicle);
  if (vehicleClass == 6 || vehicleClass == 7)
    return Architecture::DualClutch;
  return Architecture::HandlingNative;
}

void BuildSection(std::uint32_t modelHash, char (&section)[48]) {
  sprintf_s(section, "Gearbox.%08X", modelHash);
}

} // namespace

void Initialize(HMODULE pluginModule) {
  s_iniPath[0] = '\0';
  if (!pluginModule)
    return;
  const DWORD length =
      GetModuleFileNameA(pluginModule, s_iniPath, MAX_PATH);
  if (!length || length >= MAX_PATH) {
    s_iniPath[0] = '\0';
    return;
  }
  char *slash = std::strrchr(s_iniPath, '\\');
  if (!slash)
    slash = std::strrchr(s_iniPath, '/');
  if (!slash) {
    s_iniPath[0] = '\0';
    return;
  }
  slash[1] = '\0';
  strcat_s(s_iniPath, "melar-transmission.ini");
}

void SelectVehicle(Vehicle vehicle, VehicleData &data, int maxGear,
                   VehicleProfile::Drivetrain drivetrain) {
  if (vehicle == s_state.vehicle)
    return;

  if (s_state.vehicle)
    Reset();

  s_state = State{};
  s_state.vehicle = vehicle;
  s_state.modelHash =
      static_cast<std::uint32_t>(ENTITY::GET_ENTITY_MODEL(vehicle));
  s_state.gearCount = std::clamp(maxGear, 1, 16);
  const float driveForce = data.GetInitialDriveForce();
  const float clutchRateUp =
      data.GetClutchChangeRateScaleUpShift();
  const float clutchRateDown =
      data.GetClutchChangeRateScaleDownShift();
  s_state.initialDriveForce =
      driveForce > 0.0f ? driveForce : 0.30f;
  s_state.clutchRateUp =
      clutchRateUp > 0.0f ? clutchRateUp : 2.0f;
  s_state.clutchRateDown =
      clutchRateDown > 0.0f ? clutchRateDown : 2.0f;
  s_state.initialDriveMaxFlatVel = data.GetInitialDriveMaxFlatVel();
  s_state.architecture = DeriveArchitecture(vehicle, drivetrain);

  char section[48]{};
  BuildSection(s_state.modelHash, section);
  s_state.architecture =
      ParseArchitecture(section, s_state.architecture);
  s_state.finalDriveMultiplier = std::clamp(
      ReadFloat(section, "FinalDriveMultiplier", 1.0f), 0.50f, 2.00f);
  s_state.shiftSpeedMultiplier = std::clamp(
      ReadFloat(section, "ShiftSpeedMultiplier", 1.0f), 0.50f, 2.00f);
  s_state.adaptiveShiftMap =
      ReadBool(section, "AdaptiveShiftMap", true);
  s_state.predictivePreselection =
      ReadBool(section, "PredictivePreselection", true);
  s_state.skipShift = ReadBool(section, "SkipShift", true);
  s_state.thermalProtection =
      ReadBool(section, "ThermalProtection", true);

  bool changed = std::fabs(s_state.finalDriveMultiplier - 1.0f) > 0.0005f;
  for (int gear = 0; gear <= s_state.gearCount; ++gear) {
    const float original =
        data.GetGearRatio(static_cast<std::uint8_t>(gear));
    s_state.originalRatios[gear] = original;
    char key[24]{};
    sprintf_s(key, gear == 0 ? "ReverseRatio" : "Gear%dRatio", gear);
    const float configured =
        ReadFloat(section, key, original);
    const float validated =
        ClampRatio(configured, original, gear == 0);
    s_state.effectiveRatios[gear] = ClampRatio(
        validated * s_state.finalDriveMultiplier,
        original, gear == 0);
    changed = changed ||
              std::fabs(validated - original) > 0.0005f;
  }

  s_state.customRatios = changed && data.CanWriteGearRatios();
  if (changed && !s_state.customRatios) {
    LOG_WARN(Gear,
             "Custom ratios ignored for model=%08X: ratio table is not "
             "validated as per-vehicle inline memory",
             s_state.modelHash);
  }
  ApplyRatios(data);
  LOG_INFO(
      Gear,
      "Gearbox profile model=%08X type=%s gears=%d force=%.3f "
      "clutch=%.2f/%.2f flat=%.2f final=%.3f custom=%d",
      s_state.modelHash, GetArchitectureName(), s_state.gearCount,
      s_state.initialDriveForce, s_state.clutchRateUp,
      s_state.clutchRateDown, s_state.initialDriveMaxFlatVel,
      s_state.finalDriveMultiplier, s_state.customRatios ? 1 : 0);
}

void RestoreVehicle(VehicleData &data) {
  if (!s_state.ratiosApplied)
    return;
  for (int gear = 0; gear <= s_state.gearCount; ++gear) {
    const float ratio = s_state.originalRatios[gear];
    if (std::isfinite(ratio) && std::fabs(ratio) > 0.04f)
      data.SetGearRatio(static_cast<std::uint8_t>(gear), ratio);
  }
  s_state.ratiosApplied = false;
}

void Reset() {
  s_state = State{};
}

void ApplyRatios(VehicleData &data) {
  if (!s_state.customRatios || !data.CanWriteGearRatios())
    return;
  bool allApplied = true;
  bool anyApplied = false;
  for (int gear = 0; gear <= s_state.gearCount; ++gear) {
    const float ratio = s_state.effectiveRatios[gear];
    if (data.SetGearRatio(static_cast<std::uint8_t>(gear), ratio))
      anyApplied = true;
    else
      allApplied = false;
  }
  s_state.ratiosApplied = anyApplied;
  if (!allApplied) {
    RestoreVehicle(data);
    s_state.customRatios = false;
    LOG_ERROR(Gear,
              "Custom ratio transaction rolled back model=%08X",
              s_state.modelHash);
  }
}

float ResolveRatio(VehicleData &data, int gear) {
  const int index =
      gear < 0 ? 0 : std::clamp(gear, 0, s_state.gearCount);
  if (s_state.vehicle && index < static_cast<int>(
                             s_state.effectiveRatios.size())) {
    const float configured = s_state.effectiveRatios[index];
    if (std::isfinite(configured) && std::fabs(configured) > 0.04f)
      return configured;
  }
  return data.GetGearRatio(static_cast<std::uint8_t>(index));
}

void UpdateDriverModel(float throttle, float brake, float normalizedRPM,
                       int currentGear, int maxGear, float dt) {
  if (!s_state.vehicle)
    return;
  throttle = std::clamp(throttle, 0.0f, 1.0f);
  brake = std::clamp(brake, 0.0f, 1.0f);
  dt = std::clamp(dt, 0.001f, 0.05f);
  const float sample =
      std::clamp(throttle * 0.68f + brake * 0.20f +
                     std::max(0.0f, normalizedRPM - 0.55f) * 0.65f,
                 0.0f, 1.0f);
  const float tau = sample > s_state.learnedAggression ? 2.5f : 8.0f;
  const float alpha = 1.0f - std::exp(-dt / tau);
  if (s_state.adaptiveShiftMap)
    s_state.learnedAggression +=
        (sample - s_state.learnedAggression) * alpha;

  int prediction = std::clamp(currentGear, 1, std::max(1, maxGear));
  if (brake > 0.25f || throttle > 0.72f)
    prediction = std::max(1, prediction - 1);
  else if (throttle < 0.42f && normalizedRPM > 0.42f)
    prediction = std::min(maxGear, prediction + 1);
  s_state.predictedGear = prediction;
}

void NotifyShiftTarget(int targetGear) {
  s_state.predictedShiftMatched =
      s_state.predictivePreselection &&
      s_state.architecture == Architecture::DualClutch &&
      targetGear == s_state.predictedGear;
}

float GetAutomaticShiftTimeMultiplier(bool upshift, bool sport) {
  float result = 1.0f / std::max(0.50f, s_state.shiftSpeedMultiplier);
  switch (s_state.architecture) {
  case Architecture::DualClutch:
    result *= s_state.predictedShiftMatched ? 0.58f : 0.82f;
    break;
  case Architecture::DogBox:
  case Architecture::Sequential:
    result *= upshift ? 0.72f : 0.88f;
    break;
  case Architecture::TorqueConverter:
  case Architecture::HandlingNative:
    result *= sport ? 0.88f : 1.0f;
    break;
  default:
    break;
  }
  return std::clamp(result, 0.38f, 1.80f);
}

float GetAdaptiveThresholdBias() {
  return s_state.adaptiveShiftMap
             ? (s_state.learnedAggression - 0.30f) * 0.18f
             : 0.0f;
}

float GetManualPenaltyMultiplier(int fromGear, int toGear,
                                 bool clutchless) {
  if (!clutchless)
    return 1.0f;
  const bool upshift = toGear > fromGear;
  if (s_state.architecture == Architecture::DogBox)
    return upshift ? 0.22f : 0.55f;
  if (s_state.architecture == Architecture::Sequential)
    return upshift ? 0.38f : 0.72f;
  return 1.0f;
}

bool AllowsSkipShift() {
  return s_state.skipShift &&
         s_state.architecture != Architecture::CVT &&
         s_state.architecture != Architecture::SingleSpeed;
}

bool UsesTorqueConverter() {
  return s_state.architecture == Architecture::TorqueConverter ||
         s_state.architecture == Architecture::HandlingNative;
}

bool IsDualClutch() {
  return s_state.architecture == Architecture::DualClutch;
}

bool IsDogBox() {
  return s_state.architecture == Architecture::DogBox;
}

const State &GetState() {
  return s_state;
}

const char *GetArchitectureName() {
  switch (s_state.architecture) {
  case Architecture::SynchromeshManual: return "synchromesh";
  case Architecture::DogBox: return "dog-box";
  case Architecture::Sequential: return "sequential";
  case Architecture::TorqueConverter: return "torque-converter";
  case Architecture::DualClutch: return "dual-clutch";
  case Architecture::CVT: return "cvt";
  case Architecture::SingleSpeed: return "single-speed";
  default: return "handling-native";
  }
}

} // namespace GearboxProfile
