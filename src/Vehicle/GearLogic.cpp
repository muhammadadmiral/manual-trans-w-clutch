#include "GearLogic.h"
#include "../../sdk/inc/natives.h"
#include "VehicleData.h"
#include <algorithm>

namespace GearLogic {

static int s_manualGear = 0;
static DWORD s_lastShiftTime = 0; // Cooldown to prevent rapid shifting crashes

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
      (currentTime - s_lastShiftTime) > 150; // 150ms cooldown for rapid shifts

  // --- Gear Shift Logic & Clutch Check ("Gredek" / Grind sound) ---
  if (canShift) {
    if (isUp && s_manualGear < maxGear) {
      if (clutch < 0.35f && isEngineOn) {
        // Clutch not pressed: Gear Shift BLOCKED + Grind Sound!
        PlayGearGrindSound(vehicle);
        grindWarningTimer = 45;
        s_lastShiftTime = currentTime; // Prevent spamming sound every frame
      } else {
        ++s_manualGear;
        PlayGearShiftSound();
        s_lastShiftTime = currentTime;
      }
    } else if (isDown && s_manualGear > -1) {
      if (clutch < 0.35f && isEngineOn) {
        // Clutch not pressed: Gear Shift BLOCKED + Grind Sound!
        PlayGearGrindSound(vehicle);
        grindWarningTimer = 45;
        s_lastShiftTime = currentTime; // Prevent spamming sound every frame
      } else {
        --s_manualGear;
        PlayGearShiftSound();
        s_lastShiftTime = currentTime;
      }
    }
  }

  // --- Realistic Engine Stall Logic ---
  // Engine stalls ONLY if in gear (manualGear != 0), vehicle is nearly stopped
  // (< 1.5m/s), clutch is not pressed (< 0.25f), and player is not applying
  // throttle / RPM is low.
  if (isEngineOn && s_manualGear != 0) {
    const float vehicleSpeed = speedKmH / 3.6f;
    if (vehicleSpeed < 1.5f && clutch < 0.25f &&
        data.GetRPM() < 0.22f) {
      isEngineOn = false;
      VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
      // Will show notification via main loop or renderer, for now just play
      // sound
      AUDIO::PLAY_SOUND_FROM_ENTITY(-1, "BAR_OUT_OF_RANGE", vehicle,
                                    "HUD_MINIGAME_SOUNDSET", 0, 0);
    }
  }

  return s_manualGear;
}

void ApplyToMemory(Vehicle vehicle, VehicleData &data, int manualGear,
                   float clutch) {
  // We no longer use cheat power to kill torque because negative values cause the car to reverse.
  // Instead, the clutch logic is handled by cutting throttle input in InputHandler.
  VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(vehicle, 0.0f);

  if (manualGear == 0) {
    data.SetGear(1);
    data.SetNextGear(1);
    data.SetClutch(0.0f);
  } else if (manualGear == -1) {
    data.SetGear(0);
    data.SetNextGear(0);
    data.SetClutch(1.0f - clutch);
  } else {
    const uint8_t targetGear = static_cast<uint8_t>(manualGear);
    data.SetGear(targetGear);
    data.SetNextGear(targetGear);
    data.SetClutch(1.0f - clutch);
  }
}

} // namespace GearLogic
