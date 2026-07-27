#include "WorkshopTuning.h"

#include "../../Core/Config.h"
#include "../../Core/ModLogger.h"
#include "../../../sdk/inc/natives.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace WorkshopTuning {
namespace {

State s_state;
char s_iniPath[MAX_PATH]{};

int LimitFor(Option option) {
  switch (option) {
  case Option::PedalMap: return 5;
  case Option::ClutchPackage: return 3;
  case Option::Flywheel: return 3;
  case Option::Transmission: return 3;
  case Option::CreepCalibration: return 3;
  case Option::CruiseCalibration: return 3;
  case Option::DrivetrainMounts: return 3;
  case Option::TcsCalibration: return 3;
  case Option::AbsCalibration: return 3;
  case Option::LaunchCalibration: return 3;
  }
  return 0;
}

int &ValueRef(Option option) {
  switch (option) {
  case Option::PedalMap: return s_state.pedalMap;
  case Option::ClutchPackage: return s_state.clutchPackage;
  case Option::Flywheel: return s_state.flywheel;
  case Option::Transmission: return s_state.transmission;
  case Option::CreepCalibration: return s_state.creepCalibration;
  case Option::CruiseCalibration: return s_state.cruiseCalibration;
  case Option::DrivetrainMounts: return s_state.drivetrainMounts;
  case Option::TcsCalibration: return s_state.tcsCalibration;
  case Option::AbsCalibration: return s_state.absCalibration;
  case Option::LaunchCalibration: return s_state.launchCalibration;
  }
  return s_state.pedalMap;
}

void BuildSection(char (&section)[48]) {
  sprintf_s(section, "WorkshopTuning.%08X", s_state.modelHash);
}

int ReadOption(const char *section, const char *key, int fallback,
               Option option) {
  if (!s_iniPath[0])
    return fallback;
  return std::clamp(
      static_cast<int>(
          GetPrivateProfileIntA(section, key, fallback, s_iniPath)),
      0, LimitFor(option));
}

void WriteInt(const char *section, const char *key, int value) {
  if (!s_iniPath[0])
    return;
  char text[24]{};
  sprintf_s(text, "%d", value);
  WritePrivateProfileStringA(section, key, text, s_iniPath);
}

void Save() {
  char section[48]{};
  BuildSection(section);
  WriteInt(section, "PedalMap", s_state.pedalMap);
  WriteInt(section, "ClutchPackage", s_state.clutchPackage);
  WriteInt(section, "Flywheel", s_state.flywheel);
  WriteInt(section, "Transmission", s_state.transmission);
  WriteInt(section, "CreepCalibration", s_state.creepCalibration);
  WriteInt(section, "CruiseCalibration", s_state.cruiseCalibration);
  WriteInt(section, "DrivetrainMounts", s_state.drivetrainMounts);
  WriteInt(section, "TCSCalibration", s_state.tcsCalibration);
  WriteInt(section, "ABSCalibration", s_state.absCalibration);
  WriteInt(section, "LaunchCalibration", s_state.launchCalibration);
}

} // namespace

void Initialize(HMODULE module) {
  s_iniPath[0] = '\0';
  if (!module)
    return;
  const DWORD length =
      GetModuleFileNameA(module, s_iniPath, MAX_PATH);
  if (!length || length >= MAX_PATH)
    return;
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

void SelectVehicle(Vehicle vehicle) {
  if (vehicle == s_state.vehicle)
    return;
  s_state = State{};
  s_state.vehicle = vehicle;
  s_state.modelHash =
      static_cast<std::uint32_t>(ENTITY::GET_ENTITY_MODEL(vehicle));
  char section[48]{};
  BuildSection(section);
  s_state.pedalMap =
      ReadOption(section, "PedalMap", 0, Option::PedalMap);
  s_state.clutchPackage =
      ReadOption(section, "ClutchPackage", 0, Option::ClutchPackage);
  s_state.flywheel =
      ReadOption(section, "Flywheel", 0, Option::Flywheel);
  s_state.transmission =
      ReadOption(section, "Transmission", 0, Option::Transmission);
  s_state.creepCalibration =
      ReadOption(section, "CreepCalibration", 1,
                 Option::CreepCalibration);
  s_state.cruiseCalibration =
      ReadOption(section, "CruiseCalibration", 1,
                 Option::CruiseCalibration);
  s_state.drivetrainMounts =
      ReadOption(section, "DrivetrainMounts", 0,
                 Option::DrivetrainMounts);
  s_state.tcsCalibration =
      ReadOption(section, "TCSCalibration", 0,
                 Option::TcsCalibration);
  s_state.absCalibration =
      ReadOption(section, "ABSCalibration", 0,
                 Option::AbsCalibration);
  s_state.launchCalibration =
      ReadOption(section, "LaunchCalibration", 0,
                 Option::LaunchCalibration);
  Config::ApplyPedalPreset(s_state.pedalMap);
  LOG_INFO(
      Script,
      "Workshop tuning model=%08X pedal=%s clutch=%s flywheel=%s "
      "transmission=%s creep=%s cruise=%s mounts=%s",
      s_state.modelHash, GetLabel(Option::PedalMap),
      GetLabel(Option::ClutchPackage), GetLabel(Option::Flywheel),
      GetLabel(Option::Transmission), GetLabel(Option::CreepCalibration),
      GetLabel(Option::CruiseCalibration),
      GetLabel(Option::DrivetrainMounts));
}

void Reset() {
  s_state = State{};
}

void Adjust(Option option, int direction) {
  if (!s_state.vehicle || direction == 0)
    return;
  int &value = ValueRef(option);
  value = std::clamp(value + (direction > 0 ? 1 : -1),
                     0, LimitFor(option));
  if (option == Option::PedalMap)
    Config::ApplyPedalPreset(value);
  Save();
}

int GetValue(Option option) {
  return ValueRef(option);
}

const char *GetLabel(Option option) {
  static const char *pedals[] = {
      "FACTORY", "SPORT", "COMFORT", "SIM RACE", "CRAWL / VALET",
      "ECO TOURING"};
  static const char *clutches[] = {
      "OE ORGANIC", "HEAVY-DUTY", "CERAMETALLIC", "TWIN-PLATE"};
  static const char *flywheels[] = {
      "STOCK", "TOURING", "LIGHTWEIGHT", "ULTRALIGHT"};
  static const char *transmissions[] = {
      "NATIVE", "STREET", "SPORT", "RACE Q/P-SHIFT"};
  static const char *creep[] = {
      "OFF", "FACTORY", "COMFORT", "HEAVY CRAWL"};
  static const char *cruise[] = {
      "DIRECT", "FACTORY", "TOURING", "ECO COAST"};
  static const char *mounts[] = {
      "STOCK RUBBER", "REINFORCED", "SPORT", "SOLID RACE"};
  static const char *tcs[] = {
      "STREET", "WET", "SPORT", "OFF"};
  static const char *abs[] = {
      "STREET", "WET", "SPORT", "GRAVEL"};
  static const char *launch[] = {
      "FACTORY", "WET", "SPORT", "DRAG"};
  const int value = GetValue(option);
  switch (option) {
  case Option::PedalMap: return pedals[value];
  case Option::ClutchPackage: return clutches[value];
  case Option::Flywheel: return flywheels[value];
  case Option::Transmission: return transmissions[value];
  case Option::CreepCalibration: return creep[value];
  case Option::CruiseCalibration: return cruise[value];
  case Option::DrivetrainMounts: return mounts[value];
  case Option::TcsCalibration: return tcs[value];
  case Option::AbsCalibration: return abs[value];
  case Option::LaunchCalibration: return launch[value];
  }
  return "UNKNOWN";
}

const State &GetState() { return s_state; }

float GetClutchCapacityMultiplier() {
  static const float values[] = {1.0f, 1.32f, 1.55f, 1.82f};
  return values[s_state.clutchPackage];
}

float GetClutchHeatMultiplier() {
  static const float values[] = {1.0f, 0.78f, 0.62f, 0.52f};
  return values[s_state.clutchPackage];
}

float GetClutchCoolingMultiplier() {
  static const float values[] = {1.0f, 1.08f, 1.16f, 1.25f};
  return values[s_state.clutchPackage];
}

float GetClutchBiteRateMultiplier() {
  static const float clutch[] = {1.0f, 0.88f, 1.12f, 1.28f};
  static const float flywheel[] = {1.0f, 1.04f, 1.12f, 1.20f};
  return clutch[s_state.clutchPackage] * flywheel[s_state.flywheel];
}

float GetFlywheelInertiaMultiplier() {
  static const float values[] = {1.0f, 0.90f, 0.72f, 0.55f};
  return values[s_state.flywheel];
}

float GetCreepSpeedMultiplier() {
  static const float values[] = {0.0f, 1.0f, 1.12f, 0.72f};
  return values[s_state.creepCalibration];
}

float GetCreepTorqueMultiplier() {
  static const float values[] = {0.0f, 1.0f, 1.10f, 1.42f};
  return values[s_state.creepCalibration];
}

float GetCruisePedalMultiplier() {
  static const float values[] = {1.0f, 0.92f, 0.82f, 0.70f};
  return values[s_state.cruiseCalibration];
}

float GetEngineBrakeMultiplier() {
  static const float values[] = {1.10f, 1.0f, 0.82f, 0.66f};
  return values[s_state.cruiseCalibration];
}

float GetDrivetrainFlexMultiplier() {
  static const float values[] = {1.0f, 0.72f, 0.45f, 0.18f};
  return values[s_state.drivetrainMounts];
}

float GetTcsSlipMultiplier() {
  static const float values[] = {1.0f, 0.72f, 1.42f, 8.0f};
  return values[s_state.tcsCalibration];
}

float GetTcsCutMultiplier() {
  static const float values[] = {1.0f, 1.18f, 0.74f, 0.0f};
  return values[s_state.tcsCalibration];
}

float GetAbsSlipMultiplier() {
  static const float values[] = {1.0f, 0.78f, 1.18f, 1.48f};
  return values[s_state.absCalibration];
}

float GetAbsReleaseMultiplier() {
  static const float values[] = {1.0f, 1.16f, 0.86f, 0.72f};
  return values[s_state.absCalibration];
}

float GetLaunchTargetOffset() {
  static const float values[] = {0.0f, -0.08f, 0.04f, 0.10f};
  return values[s_state.launchCalibration];
}

float GetLaunchCutAggression() {
  static const float values[] = {0.72f, 0.88f, 0.62f, 0.52f};
  return values[s_state.launchCalibration];
}

float GetTransmissionStage() {
  static const float values[] = {0.0f, 0.34f, 0.67f, 1.0f};
  return values[s_state.transmission];
}

bool IsRaceTransmission() {
  return s_state.transmission == 3;
}

} // namespace WorkshopTuning
