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
  // Engine stalls ONLY if in gear (manualGear != 0), vehicle is nearly stopped
  // (< 1.5m/s), clutch is not pressed (< 0.25f), and player is not applying
  // throttle / RPM is low.
  if (isEngineOn && s_manualGear != 0) {
    const float vehicleSpeed = speedKmH / 3.6f;
    if (vehicleSpeed < 1.5f && clutch < 0.25f && data.GetRPM() < 0.22f) {
      isEngineOn = false;
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
                   float clutch, float throttle) {
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
  if (clutchWasOpen && !clutchOpen && shiftArmed) {
    nativeShiftUntil = GetTickCount() + 240;
    shiftArmed = false;
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

  // 0x8CC follows RPM in the captured log, so it is not a safe writable
  // clutch field on this build.  For neutral or a fully depressed clutch,
  // synthesize a free-revving engine while ApplyGameControls removes wheel
  // torque. This also avoids fighting GTA's native hard-cut limiter.
  const bool disconnected = manualGear == 0 || clutchOpen;
  if (disconnected) {
    static float freeRevRPM = 0.20f;
    const float targetRPM =
        0.20f + std::clamp(throttle, 0.0f, 1.0f) * 0.78f;
    const float response = targetRPM > freeRevRPM ? 0.16f : 0.08f;
    freeRevRPM += (targetRPM - freeRevRPM) * response;
    data.SetRPM(std::clamp(freeRevRPM, 0.20f, 0.98f));
  } else {
    // Once the clutch reconnects, stop writing RPM entirely. Native GTA
    // already derives the correct RPM and audio pitch from road speed and the
    // selected gear; forcing an estimated ratio here caused idle lockups.
    // Once a driven gear reaches redline, hold just below GTA's native hard
    // cut instead of allowing the stock limiter to drop and rebuild RPM.
    static int heldGear = 0;
    static bool holdingRedline = false;
    if (manualGear != heldGear || throttle < 0.85f) {
      heldGear = manualGear;
      holdingRedline = false;
    }
    const bool nativeShiftActive = GetTickCount() < nativeShiftUntil;
    if (!nativeShiftActive && manualGear > 0 && throttle >= 0.85f &&
        data.GetRPM() >= 0.94f)
      holdingRedline = true;
    if (holdingRedline && !nativeShiftActive)
      data.SetRPM(0.98f);
  }
}

} // namespace GearLogic
