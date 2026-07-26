#include "VehicleProfile.h"
#include "../../sdk/inc/natives.h"

namespace VehicleProfile {

static bool IsScooterModel(Hash model) {
  static const Hash scooterModels[] = {
      MISC::GET_HASH_KEY("PIZZABOY")
  };
  for (const Hash scooter : scooterModels)
    if (model == scooter)
      return true;
  return false;
}

static bool IsUtilitySingleSpeedModel(Hash model) {
  static const Hash utilityModels[] = {
      MISC::GET_HASH_KEY("AIRTUG"),
      MISC::GET_HASH_KEY("BAGGAGE"),
      MISC::GET_HASH_KEY("BAGGAGE2"),
      MISC::GET_HASH_KEY("BULLDOZER"),
      MISC::GET_HASH_KEY("CADDY"),
      MISC::GET_HASH_KEY("CADDY2"),
      MISC::GET_HASH_KEY("CADDY3"),
      MISC::GET_HASH_KEY("DLOADER"),
      MISC::GET_HASH_KEY("DOCKTUG"),
      MISC::GET_HASH_KEY("FORKLIFT"),
      MISC::GET_HASH_KEY("HANDLER"),
      MISC::GET_HASH_KEY("MOWER"),
      MISC::GET_HASH_KEY("RIPLEY")
  };
  for (const Hash utility : utilityModels)
    if (model == utility)
      return true;
  return false;
}

Drivetrain Detect(Vehicle vehicle) {
  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
  if (VEHICLE::_GET_IS_VEHICLE_ELECTRIC(model))
    return Drivetrain::Electric;
  if (IsUtilitySingleSpeedModel(model))
    return Drivetrain::UtilitySingleSpeed;
  if (IsScooterModel(model))
    return Drivetrain::ScooterCVT;
  if (VEHICLE::IS_THIS_MODEL_A_BIKE(model))
    return Drivetrain::MotorcycleSequential;
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
