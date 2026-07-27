#include "ManualGearbox.h"
#include "../../../../sdk/inc/natives.h"
#include "../../../Core/Config.h"
#include "../../../Core/ModLogger.h"
#include "../../../Script/DrivingEventBus.h"
#include "../../VehicleData.h"
#include "../Core/GearboxSystem.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>


namespace ManualGearbox {

static int s_manualGear = 0;
static DWORD s_lastShiftTime = 0;
static int s_pendingGear = 0;
static DWORD s_pendingAt = 0;

void PlayGearGrindSound(Vehicle vehicle) {
  DrivingEventBus::EventData event{};
  event.vehicle = vehicle;
  DrivingEventBus::Publish(DrivingEventBus::Event::GearGrind, event);
}

void PlayGearShiftSound(Vehicle vehicle, int fromGear, int toGear, float clutch,
                        float throttle) {
  const bool upshift = toGear > fromGear;
  const auto &shift = GearboxSystem::GetState();
  const bool harsh = shift.quickShift || shift.powerShift ||
                     shift.clashSeverity > 0.35f || shift.moneyShift ||
                     (throttle > 0.82f && clutch < 0.42f);
  const bool slow = !harsh && (clutch > 0.70f || throttle < 0.22f);
  DrivingEventBus::EventData event{};
  event.vehicle = vehicle;
  event.fromGear = fromGear;
  event.toGear = toGear;
  event.severity = harsh ? 0.90f : (slow ? 0.15f : 0.45f);
  event.quickShift = shift.quickShift;
  DrivingEventBus::Publish(upshift ? DrivingEventBus::Event::GearShiftUp
                                   : DrivingEventBus::Event::GearShiftDown,
                           event);
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
  const bool canShift = (currentTime - s_lastShiftTime) > 250;
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
        LOG_WARN(Gear, "Synchro menolak shift %d -> %d wear=%.3f", fromGear,
                 toGear, GearboxSystem::GetState().selectedSynchroWear);
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
        LOG_WARN(Gear, "Reverse lockout: speed=%.1fkm/h limit=%.1f", speedKmH,
                 Config::ReverseLockoutSpeedKmH);
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
        LOG_WARN(Gear, "Synchro menolak shift %d -> %d wear=%.3f", fromGear,
                 toGear, GearboxSystem::GetState().selectedSynchroWear);
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
  // Neutral tidak punya representasi forward khusus di CVehicle, jadi gear 1
  // hanya menjadi carrier ketika logical gear memang N. Saat pedal kopling
  // diinjak di gear 2+, pertahankan gear pilihan pengemudi: menulis gear 1 di
  // sini membuat GTA melakukan satu shift tersembunyi lalu satu shift lagi
  // ketika kopling dilepas.
  if (manualGear == 0) {
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
  (void)throttle;
  (void)speedKmH;
  (void)clutch;
}

} // namespace ManualGearbox
