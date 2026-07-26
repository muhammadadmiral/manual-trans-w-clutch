#include "GearLogic.h"
#include "../../../sdk/inc/natives.h"
#include "../../Core/ModLogger.h"
#include "../../Core/Config.h"
#include "../../Audio/AudioEngine.h"
#include "../VehicleData.h"
#include "GearboxSystem.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace GearLogic {

static int s_manualGear = 0;
static DWORD s_lastShiftTime = 0;
static int s_pendingGear = 0;
static DWORD s_pendingAt = 0;

void PlayGearGrindSound(Vehicle vehicle) {
  if (AudioEngine::PlayGearGrind(vehicle))
    return;
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

void PlayGearShiftSound(Vehicle vehicle, int fromGear, int toGear,
                        float clutch, float throttle) {
  const bool upshift = toGear > fromGear;
  const bool powerShift = throttle > 0.78f;
  const bool softShift = clutch > 0.70f || throttle < 0.18f;
  if (AudioEngine::PlayManualShift(vehicle, upshift, powerShift, softShift))
    return;
  AUDIO::PLAY_SOUND_FRONTEND(-1, "NAV_LEFT_RIGHT",
                             "HUD_FRONTEND_DEFAULT_SOUNDSET", 1);
}

void Reset(int defaultGear) {
  s_manualGear = defaultGear;
  s_lastShiftTime = GetTickCount();
  s_pendingGear = defaultGear;
  s_pendingAt = 0;
}

int Update(Vehicle vehicle, VehicleData &data, int maxGear, bool isUp,
           bool isDown, float clutch, float throttle, float speedKmH,
           bool &isEngineOn, int &grindWarningTimer) {
  const DWORD currentTime = GetTickCount();
  const bool canShift =
      (currentTime - s_lastShiftTime) > 80; // permit quick multi-gear selection

  if (s_pendingAt != 0 && currentTime >= s_pendingAt) {
    const int fromGear = s_manualGear;
    const int toGear = s_pendingGear;
    GearboxSystem::NotifyShift(vehicle, data, fromGear, toGear, clutch,
                               throttle);
    s_manualGear = toGear;
    PlayGearShiftSound(vehicle, fromGear, toGear, clutch, throttle);
    s_pendingAt = 0;
    s_lastShiftTime = currentTime;
    LOG_INFO(Gear, "Delayed synchro engagement: %d -> %d", fromGear, toGear);
  }

  if (canShift && (isUp || isDown) && GearboxSystem::IsSeized()) {
    PlayGearGrindSound(vehicle);
    grindWarningTimer = 60;
    s_lastShiftTime = currentTime;
    LOG_ERROR(Gear, "Gearbox seized: shift rejected");
    return s_manualGear;
  }

  if (canShift && s_pendingAt == 0) {
    if (isUp && s_manualGear < maxGear) {
      const int fromGear = s_manualGear;
      const int toGear = s_manualGear + 1;
      const bool clutchless = clutch < 0.35f && isEngineOn;
      const uint32_t resistance = GearboxSystem::GetShiftResistanceMs(
          data, fromGear, toGear, clutch, throttle);
      if (resistance == (std::numeric_limits<uint32_t>::max)()) {
        PlayGearGrindSound(vehicle);
        grindWarningTimer = 60;
        s_lastShiftTime = currentTime;
        LOG_WARN(Gear, "Synchro menolak shift %d -> %d wear=%.3f",
                 fromGear, toGear,
                 GearboxSystem::GetState().selectedSynchroWear);
        return s_manualGear;
      }
      if (resistance > 0) {
        s_pendingGear = toGear;
        s_pendingAt = currentTime + resistance;
        s_lastShiftTime = currentTime;
        return s_manualGear;
      }
      GearboxSystem::NotifyShift(vehicle, data, fromGear, toGear, clutch,
                                 throttle);
      s_manualGear = toGear;
      if (clutchless && !GearboxSystem::GetState().quickShift &&
          !GearboxSystem::GetState().synchroShift) {
        PlayGearGrindSound(vehicle);
        grindWarningTimer = 45;
      } else {
        PlayGearShiftSound(vehicle, fromGear, toGear, clutch, throttle);
      }
      s_lastShiftTime = currentTime;
    } else if (isDown && s_manualGear > -1) {
      if (s_manualGear == 0 &&
          std::fabs(speedKmH) >
              (std::max)(0.0f, Config::ReverseLockoutSpeedKmH)) {
        PlayGearGrindSound(vehicle);
        grindWarningTimer = 45;
        LOG_WARN(Gear, "Reverse lockout: speed=%.1fkm/h limit=%.1f",
                 speedKmH, Config::ReverseLockoutSpeedKmH);
        s_lastShiftTime = currentTime;
        return s_manualGear;
      }
      const int fromGear = s_manualGear;
      const int toGear = s_manualGear - 1;
      const bool clutchless = clutch < 0.35f && isEngineOn;
      const uint32_t resistance = GearboxSystem::GetShiftResistanceMs(
          data, fromGear, toGear, clutch, throttle);
      if (resistance == (std::numeric_limits<uint32_t>::max)()) {
        PlayGearGrindSound(vehicle);
        grindWarningTimer = 60;
        s_lastShiftTime = currentTime;
        LOG_WARN(Gear, "Synchro menolak shift %d -> %d wear=%.3f",
                 fromGear, toGear,
                 GearboxSystem::GetState().selectedSynchroWear);
        return s_manualGear;
      }
      if (resistance > 0) {
        s_pendingGear = toGear;
        s_pendingAt = currentTime + resistance;
        s_lastShiftTime = currentTime;
        return s_manualGear;
      }
      GearboxSystem::NotifyShift(vehicle, data, fromGear, toGear, clutch,
                                 throttle);
      s_manualGear = toGear;
      if (clutchless && !GearboxSystem::GetState().quickShift &&
          !GearboxSystem::GetState().synchroShift) {
        PlayGearGrindSound(vehicle);
        grindWarningTimer = 45;
      } else {
        PlayGearShiftSound(vehicle, fromGear, toGear, clutch, throttle);
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
  // Gear 1 dipakai sebagai carrier saat drivetrain terbuka karena GTA bisa
  // menganggap 0xFF sebagai permintaan auto-forward. Clutch signed yang
  // benar-benar memutus roda; logical gear tetap tidak berubah.
  if (manualGear == 0 || clutch >= 0.88f) {
    data.SetGear(1);
    data.SetNextGear(1);
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
