#include "GearLogic.h"
#include "../../sdk/inc/natives.h"
#include "../Core/ModLogger.h"
#include "VehicleData.h"
#include <algorithm>

namespace GearLogic {

static int s_manualGear = 0;
static DWORD s_lastShiftTime = 0; // Cooldown to prevent rapid shifting crashes
static DWORD s_lowRpmSince = 0;

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
  s_lowRpmSince = 0;
}

int Update(Vehicle vehicle, VehicleData &data, int maxGear, bool isUp,
           bool isDown, float clutch, float throttle, float speedKmH,
           bool &isEngineOn, int &grindWarningTimer) {
  const DWORD currentTime = GetTickCount();
  const bool canShift =
      (currentTime - s_lastShiftTime) > 80; // permit quick multi-gear selection

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
  // A stall is a sustained unloaded bog, not one transient RPM sample during
  // clutch hook-up. Full throttle must be allowed to launch in first or second.
  if (isEngineOn && s_manualGear != 0) {
    const float vehicleSpeed = speedKmH / 3.6f;
    const bool bogging = vehicleSpeed < 1.5f && clutch < 0.12f &&
                         throttle < 0.08f && data.GetRPM() < 0.16f;
    if (bogging) {
      if (s_lowRpmSince == 0) s_lowRpmSince = currentTime;
    } else {
      s_lowRpmSince = 0;
    }
    if (s_lowRpmSince != 0 && currentTime - s_lowRpmSince > 450) {
      isEngineOn = false;
      s_lowRpmSince = 0;
      VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
      // Will show notification via main loop or renderer, for now just play
      // sound
      AUDIO::PLAY_SOUND_FROM_ENTITY(-1, "BAR_OUT_OF_RANGE", vehicle,
                                    "HUD_MINIGAME_SOUNDSET", 0, 0);
    }
  }

  // --- Gear Ratio Over-Rev Simulation ---
  if (s_manualGear > 0 && s_manualGear <= maxGear) {
    float currentRatio = data.GetGearRatio(s_manualGear);
    float topRatio = data.GetGearRatio(maxGear);

    // Very simplified approximation: if ratio is valid and we are moving fast
    if (currentRatio > 0.0f && topRatio > 0.0f) {
      // Relative ratio compares current gear to top gear
      float relativeRatio = currentRatio / topRatio;

      // If speed is very high but we are in a very low gear, trigger over-rev
      // limit
      float estimatedSpeedRatio =
          speedKmH / 300.0f; // Assume ~300km/h max speed for typical supercars
      float estimatedRPM = estimatedSpeedRatio * relativeRatio;

      if (estimatedRPM > 1.2f && clutch < 0.5f) {
        // OVER-REV! The player downshifted too early at high speed.
        // We can simulate engine braking by applying a brake force natively,
        // or just playing a warning sound and killing the engine.
        // For now, let's stall the engine if they severely over-rev it to
        // protect the engine!
        if (isEngineOn) {
          isEngineOn = false;
          VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
          PlayGearGrindSound(vehicle);
          grindWarningTimer = 60; // Show warning
        }
      }
    }
  }

  return s_manualGear;
}

void ApplyToMemory(Vehicle vehicle, VehicleData &data, int manualGear,
                   int maxGear, float clutch, float throttle, float speedKmH) {
  // We do not override engine torque here. 
  // Keep the native gearbox in memory-neutral through the upper half of the
  // pedal travel. Below this bite point the selected gear reconnects and the
  // control-layer engagement ramp transfers torque progressively.
  const bool clutchOpen = clutch > 0.45f;
  static int selectedLastFrame = 0;
  static int shiftFromGear = 0;
  static int shiftToGear = 0;
  static bool shiftArmed = false;
  static bool clutchWasOpen = false;
  static DWORD nativeShiftUntil = 0;

  if (manualGear != selectedLastFrame) {
    shiftFromGear = selectedLastFrame;
    shiftToGear = manualGear;
    shiftArmed = true;
    selectedLastFrame = manualGear;
  }

  // Releasing the pedal through the bite point commits the preselected gear.
  // Request it as Current != Next for a short window so GTA's own transmission
  // code performs its torque cut, RPM transition and stock shift audio.
  if (clutchWasOpen && !clutchOpen) {
    if (shiftArmed) {
      nativeShiftUntil = GetTickCount() + 240;
      shiftArmed = false;
      LOG_INFO(Gear,
               "NATIVE_SHIFT_REQUEST: from=%d to=%d rpm=%.3f clutch=%.3f "
               "windowMs=240",
               shiftFromGear, shiftToGear, data.GetRPM(), clutch);
    }
  }
  clutchWasOpen = clutchOpen;

  if (manualGear == 0 || clutchOpen) {
    // 0xFF is GTA V's neutral sentinel.  Do not emulate neutral by selecting
    // first gear. A fully depressed clutch also uses temporary memory-neutral:
    // the selected manual gear is retained and re-engaged on pedal release.
    data.SetGear(0xFF);
    data.SetNextGear(0xFF);
  } else if (GetTickCount() < nativeShiftUntil && shiftToGear != 0) {
    const uint8_t nativeFrom = shiftFromGear < 0
        ? 0
        : (shiftFromGear == 0 ? 0xFF
                              : static_cast<uint8_t>(shiftFromGear));
    const uint8_t nativeTo =
        shiftToGear < 0 ? 0 : static_cast<uint8_t>(shiftToGear);
    data.SetGear(nativeFrom);
    data.SetNextGear(nativeTo);
  } else if (manualGear == -1) {
    // Reverse: GTA V uses Gear 0 for reverse.
    data.SetGear(0);
    data.SetNextGear(0);
  } else {
    // Forward gears
    const uint8_t targetGear = static_cast<uint8_t>(manualGear);
    data.SetGear(targetGear);
    data.SetNextGear(targetGear);
  }

  // RPM offset 0x8CC is read-only on this game build. Native GTA now owns
  // revving, limiter, and all engine audio in N/R/forward gears. This avoids
  // a synthetic RPM state fighting the actual drivetrain and causing stalls.
}

} // namespace GearLogic
