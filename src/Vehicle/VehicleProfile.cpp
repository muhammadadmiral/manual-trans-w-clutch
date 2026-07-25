#include "VehicleProfile.h"
#include "../../sdk/inc/natives.h"

namespace VehicleProfile {

static bool IsScooterModel(Hash model) {
  static const Hash scooterModels[] = {
      MISC::GET_HASH_KEY("FAGGIO"),
      MISC::GET_HASH_KEY("FAGGIO2"),
      MISC::GET_HASH_KEY("FAGGIO3"),
      MISC::GET_HASH_KEY("PIZZABOY")
  };
  for (const Hash scooter : scooterModels)
    if (model == scooter)
      return true;
  return false;
}

Drivetrain Detect(Vehicle vehicle) {
  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
  if (VEHICLE::_GET_IS_VEHICLE_ELECTRIC(model))
    return Drivetrain::Electric;
  if (IsScooterModel(model))
    return Drivetrain::ScooterCVT;
  if (VEHICLE::IS_THIS_MODEL_A_BIKE(model))
    return Drivetrain::MotorcycleSequential;
  return Drivetrain::Standard;
}

bool ForcesAutomatic(Drivetrain profile) {
  return profile == Drivetrain::ScooterCVT ||
         profile == Drivetrain::Electric;
}

bool UsesAutomaticClutch(Drivetrain profile) {
  return profile == Drivetrain::ScooterCVT ||
         profile == Drivetrain::MotorcycleSequential ||
         profile == Drivetrain::Electric;
}

const char *GetName(Drivetrain profile) {
  switch (profile) {
  case Drivetrain::ScooterCVT:
    return "scooter-cvt";
  case Drivetrain::MotorcycleSequential:
    return "motorcycle-sequential";
  case Drivetrain::Electric:
    return "electric";
  default:
    return "standard";
  }
}

} // namespace VehicleProfile
