#pragma once

#define NOMINMAX
#include <Windows.h>

#include <array>
#include <cstdint>

class VehicleData;
using Vehicle = int;

namespace VehicleProfile {
enum class Drivetrain;
}

namespace GearboxProfile {

enum class Architecture : int {
  HandlingNative = 0,
  SynchromeshManual,
  DogBox,
  Sequential,
  TorqueConverter,
  DualClutch,
  CVT,
  SingleSpeed
};

struct State {
  Vehicle vehicle = 0;
  std::uint32_t modelHash = 0;
  Architecture architecture = Architecture::HandlingNative;
  int gearCount = 0;

  float initialDriveForce = 0.30f;
  float clutchRateUp = 2.0f;
  float clutchRateDown = 2.0f;
  float initialDriveMaxFlatVel = 0.0f;
  float finalDriveMultiplier = 1.0f;
  float shiftSpeedMultiplier = 1.0f;

  float learnedAggression = 0.0f;
  int predictedGear = 1;
  bool predictedShiftMatched = false;
  bool adaptiveShiftMap = true;
  bool predictivePreselection = true;
  bool skipShift = true;
  bool thermalProtection = true;
  bool customRatios = false;
  bool ratiosApplied = false;

  std::array<float, 17> originalRatios{};
  std::array<float, 17> effectiveRatios{};
};

// Stores the ASI directory used for per-model [Gearbox.<hash>] overrides in
// melar-transmission.ini. Safe to call more than once.
void Initialize(HMODULE pluginModule);

// Selects and snapshots one CVehicle. Defaults are derived from handling.meta;
// optional per-model overrides are then layered on top.
void SelectVehicle(Vehicle vehicle, VehicleData &data, int maxGear,
                   VehicleProfile::Drivetrain drivetrain);
void RestoreVehicle(VehicleData &data);
void Reset();

// Re-applies validated per-instance ratios if GTA rebuilt its transmission.
// Pointer-backed/shared ratio tables are deliberately never written.
void ApplyRatios(VehicleData &data);
float ResolveRatio(VehicleData &data, int gear);

// Driver-learning and DCT preselection. This does not guess a shift on its own;
// it only supplies a physically useful bias to AutomaticGearbox.
void UpdateDriverModel(float throttle, float brake, float normalizedRPM,
                       int currentGear, int maxGear, float dt);
void NotifyShiftTarget(int targetGear);
float GetAutomaticShiftTimeMultiplier(bool upshift, bool sport);
float GetAdaptiveThresholdBias();
float GetManualPenaltyMultiplier(int fromGear, int toGear,
                                 bool clutchless);
bool AllowsSkipShift();
bool UsesTorqueConverter();
bool IsDualClutch();
bool IsDogBox();

const State &GetState();
const char *GetArchitectureName();

} // namespace GearboxProfile
