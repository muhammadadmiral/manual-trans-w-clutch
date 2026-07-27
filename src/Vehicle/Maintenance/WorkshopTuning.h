#pragma once

#define NOMINMAX
#include <Windows.h>

#include <cstdint>

using Vehicle = int;

namespace WorkshopTuning {

enum class Option : int {
  PedalMap = 0,
  ClutchPackage,
  Flywheel,
  Transmission,
  CreepCalibration,
  CruiseCalibration,
  DrivetrainMounts,
  TcsCalibration,
  AbsCalibration,
  LaunchCalibration
};

struct State {
  Vehicle vehicle = 0;
  std::uint32_t modelHash = 0;
  int pedalMap = 0;
  int clutchPackage = 0;
  int flywheel = 0;
  int transmission = 0;
  int creepCalibration = 1;
  int cruiseCalibration = 1;
  int drivetrainMounts = 0;
  int tcsCalibration = 0;
  int absCalibration = 0;
  int launchCalibration = 0;
};

void Initialize(HMODULE module);
void SelectVehicle(Vehicle vehicle);
void Reset();
void Adjust(Option option, int direction);
int GetValue(Option option);
const char *GetLabel(Option option);
const State &GetState();

float GetClutchCapacityMultiplier();
float GetClutchHeatMultiplier();
float GetClutchCoolingMultiplier();
float GetClutchBiteRateMultiplier();
float GetFlywheelInertiaMultiplier();
float GetCreepSpeedMultiplier();
float GetCreepTorqueMultiplier();
float GetCruisePedalMultiplier();
float GetEngineBrakeMultiplier();
float GetDrivetrainFlexMultiplier();
float GetTcsSlipMultiplier();
float GetTcsCutMultiplier();
float GetAbsSlipMultiplier();
float GetAbsReleaseMultiplier();
float GetLaunchTargetOffset();
float GetLaunchCutAggression();
float GetTransmissionStage();
bool IsRaceTransmission();

} // namespace WorkshopTuning
