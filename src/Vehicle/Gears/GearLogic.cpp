#include "GearLogic.h"
#include "../../../sdk/inc/natives.h"
#include "../../Core/ModLogger.h"
#include "../VehicleData.h"
#include "GearboxSystem.h"
#include <algorithm>

namespace GearLogic {

static int s_manualGear = 0;
static DWORD s_lastShiftTime = 0;

void PlayGearGrindSound(Vehicle vehicle) {
  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
  if (VEHICLE::IS_THIS_MODEL_A_BIKE(model) ||
      VEHICLE::IS_THIS_MODEL_A_QUADBIKE(model)) {
    AUDIO::PLAY_SOUND_FROM_ENTITY(-1, "NAV_UP_DOWN", vehicle,
                                  "HUD_FREEMODE_SOUNDSET", 0, 0);
  } else {
    AUDIO::PLAY_SOUND_FROM_ENTITY(-1, "BAR_OUT_OF_RANGE", vehicle,
                                  "HUD_MINIGAME_SOUNDSET", 0, 0);
  }
}

void PlayGearShiftSound() {
  AUDIO::PLAY_SOUND_FRONTEND(-1, "NAV_LEFT_RIGHT",
                             "HUD_FRONTEND_DEFAULT_SOUNDSET", 1);
}

void Reset(int defaultGear) {
  s_manualGear = defaultGear;
  s_lastShiftTime = GetTickCount();
}

int Update(Vehicle vehicle, VehicleData &data, int maxGear, bool isUp,
           bool isDown, float clutch, float throttle, float speedKmH,
           bool &isEngineOn, int &grindWarningTimer) {
  const DWORD currentTime = GetTickCount();
  const bool canShift =
      (currentTime - s_lastShiftTime) > 80; // permit quick multi-gear selection

  if (canShift) {
    if (isUp && s_manualGear < maxGear) {
      const int fromGear = s_manualGear;
      const int toGear = s_manualGear + 1;
      const bool clutchless = clutch < 0.35f && isEngineOn;
      GearboxSystem::NotifyShift(data, fromGear, toGear, clutch, throttle);
      s_manualGear = toGear;
      if (clutchless) {
        PlayGearGrindSound(vehicle);
        grindWarningTimer = 45;
      } else {
        PlayGearShiftSound();
      }
      s_lastShiftTime = currentTime;
    } else if (isDown && s_manualGear > -1) {
      const int fromGear = s_manualGear;
      const int toGear = s_manualGear - 1;
      const bool clutchless = clutch < 0.35f && isEngineOn;
      GearboxSystem::NotifyShift(data, fromGear, toGear, clutch, throttle);
      s_manualGear = toGear;
      if (clutchless) {
        PlayGearGrindSound(vehicle);
        grindWarningTimer = 45;
      } else {
        PlayGearShiftSound();
      }
      s_lastShiftTime = currentTime;
    }
  }

  (void)maxGear;
  (void)speedKmH;
  (void)isEngineOn;
  return s_manualGear;
}

void ApplyToMemory(Vehicle vehicle, VehicleData &data, int manualGear,
                   int maxGear, float clutch, float throttle, float speedKmH) {
  if (manualGear == 0) {
    data.SetGear(0xFF);
    data.SetNextGear(0xFF);
  } else if (manualGear == -1) {
    data.SetGear(0);
    data.SetNextGear(0);
  } else {
    const uint8_t targetGear = static_cast<uint8_t>(manualGear);
    data.SetGear(targetGear);
    data.SetNextGear(targetGear);
  }
  (void)vehicle;
  (void)maxGear;
  (void)clutch;
  (void)throttle;
  (void)speedKmH;
}

} // namespace GearLogic
