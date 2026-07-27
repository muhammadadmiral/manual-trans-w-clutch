#include "VehicleProfile.h"
#include "../../sdk/inc/natives.h"

namespace VehicleProfile {

Drivetrain Detect(Vehicle vehicle) {
  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
  if (VEHICLE::_GET_IS_VEHICLE_ELECTRIC(model))
    return Drivetrain::Electric;
  const int nativeGearCount =
      VEHICLE::_GET_VEHICLE_MODEL_NUM_DRIVE_GEARS(model);
  if (VEHICLE::IS_THIS_MODEL_A_BIKE(model)) {
    return nativeGearCount <= 1 ? Drivetrain::ScooterCVT
                                : Drivetrain::MotorcycleSequential;
  }
  if (nativeGearCount == 1)
    return Drivetrain::UtilitySingleSpeed;
  return Drivetrain::Standard;
}

bool ForcesAutomatic(Drivetrain profile) {
  return profile == Drivetrain::ScooterCVT ||
         profile == Drivetrain::Electric ||
         profile == Drivetrain::UtilitySingleSpeed;
}

bool UsesAutomaticClutch(Drivetrain profile) {
  return profile == Drivetrain::ScooterCVT ||
         profile == Drivetrain::MotorcycleSequential ||
         profile == Drivetrain::Electric ||
         profile == Drivetrain::UtilitySingleSpeed;
}

const char *GetName(Drivetrain profile) {
  switch (profile) {
  case Drivetrain::ScooterCVT:
    return "scooter-cvt";
  case Drivetrain::MotorcycleSequential:
    return "motorcycle-sequential";
  case Drivetrain::Electric:
    return "electric";
  case Drivetrain::UtilitySingleSpeed:
    return "utility-single-speed";
  default:
    return "standard";
  }
}

} // namespace VehicleProfile
