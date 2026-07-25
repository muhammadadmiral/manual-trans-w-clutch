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
                   float clutch) {
  // We do not override engine torque here. 

  // GTA V fClutch: 1.0 = Engaged (Plates clamped), 0.0 = Open (Plates separated)
  // simulatedClutch (clutch parameter): 1.0 = Pedal pressed (Open), 0.0 = Pedal released (Engaged)
  const float memoryClutch = 1.0f - clutch;

  if (manualGear == 0) {
    // Neutral: 0xFF is the actual neutral gear in GTA V memory.
    // We force the clutch to fully OPEN (0.0f) so no torque can reach the wheels,
    // regardless of whether the pedal is pressed or not.
    data.SetGear(0xFF);
    data.SetNextGear(0xFF);
    data.SetTopGear(0xFF);
    data.SetClutch(0.0f); 
  } else if (manualGear == -1) {
    // Reverse: GTA V uses Gear 0 for reverse.
    data.SetGear(0);
    data.SetNextGear(0);
    data.SetTopGear(0);
    data.SetClutch(memoryClutch); 
  } else {
    // Forward gears
    const uint8_t targetGear = static_cast<uint8_t>(manualGear);
    data.SetGear(targetGear);
    data.SetNextGear(targetGear);
    data.SetTopGear(targetGear);
    data.SetClutch(memoryClutch);
  }
}

} // namespace GearLogic
