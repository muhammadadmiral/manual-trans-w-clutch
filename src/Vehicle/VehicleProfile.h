#pragma once

using Vehicle = int;

namespace VehicleProfile {

enum class Drivetrain {
  Standard,
  ScooterCVT,
  MotorcycleSequential,
  Electric
};

Drivetrain Detect(Vehicle vehicle);
bool ForcesAutomatic(Drivetrain profile);
bool UsesAutomaticClutch(Drivetrain profile);
const char *GetName(Drivetrain profile);

} // namespace VehicleProfile
