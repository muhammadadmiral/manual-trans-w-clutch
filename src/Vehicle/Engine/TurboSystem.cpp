// =============================================================================
// TurboSystem.cpp
// Turbo simulation logic. ModKit id 18 is the Turbo in GTA V.
// =============================================================================
#include "TurboSystem.h"
#include "../VehicleData.h"
#include "../../Core/Config.h"
#include "../../Script/DrivingEventBus.h"
#include "../../../sdk/inc/natives.h"
#include <algorithm>

namespace TurboSystem {

static TurboState s_state;

static inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void Reset() {
  if (s_state.vehicle && ENTITY::DOES_ENTITY_EXIST(s_state.vehicle) &&
      s_state.nativeBoostActive)
    AUDIO::SET_VEHICLE_BOOST_ACTIVE(s_state.vehicle, FALSE);
  s_state = TurboState{};
}

void InitializeForVehicle(Vehicle vehicle) {
  if (s_state.vehicle && s_state.vehicle != vehicle &&
      ENTITY::DOES_ENTITY_EXIST(s_state.vehicle) &&
      s_state.nativeBoostActive)
    AUDIO::SET_VEHICLE_BOOST_ACTIVE(s_state.vehicle, FALSE);
  s_state.vehicle = vehicle;
  s_state.hasTurbo = VEHICLE::IS_TOGGLE_MOD_ON(vehicle, 18) != FALSE;
}

float Update(Vehicle vehicle, VehicleData &data, float rpm, float throttle, bool isEngineOn) {
  if (!s_state.hasTurbo || !isEngineOn) {
    if (s_state.nativeBoostActive && ENTITY::DOES_ENTITY_EXIST(vehicle))
      AUDIO::SET_VEHICLE_BOOST_ACTIVE(vehicle, FALSE);
    s_state.nativeBoostActive = false;
    s_state.spool = 0.0f;
    s_state.boostPressure = 0.0f;
    s_state.previousThrottle = 0.0f;
    return 1.0f; // 1.0x power multiplier (no effect)
  }

  const float dt = std::clamp(MISC::GET_FRAME_TIME(), 0.001f, 0.05f);
  const float exhaustFlow =
      Clamp01((rpm - 0.22f) / 0.78f) * Clamp01(throttle);
  const float targetSpool = exhaustFlow;

  if (targetSpool > s_state.spool) {
    s_state.spool += dt * (0.55f + rpm * 0.85f);
    if (s_state.spool > targetSpool) s_state.spool = targetSpool;
    s_state.blowOffLatched = false;
    s_state.flutterLatched = false;
  } else {
    float diff = s_state.spool - targetSpool;
    if (diff > 0.3f && throttle < 0.1f && !s_state.blowOffLatched) {
      DrivingEventBus::EventData event{};
      event.vehicle = vehicle;
      event.severity = std::clamp(diff, 0.0f, 1.0f);
      DrivingEventBus::Publish(
          DrivingEventBus::Event::TurboBlowoff, event);
      s_state.blowOffLatched = true;
    } else if (s_state.spool > 0.42f &&
               s_state.previousThrottle - throttle > 0.24f &&
               throttle >= 0.08f && throttle < 0.58f &&
               !s_state.flutterLatched) {
      DrivingEventBus::EventData event{};
      event.vehicle = vehicle;
      event.severity =
          std::clamp(s_state.spool *
                         (s_state.previousThrottle - throttle),
                     0.0f, 1.0f);
      DrivingEventBus::Publish(
          DrivingEventBus::Event::TurboFlutter, event);
      s_state.flutterLatched = true;
    }
    s_state.spool -= dt * (throttle < 0.1f ? 2.8f : 1.2f);
    if (s_state.spool < targetSpool) s_state.spool = targetSpool;
  }
  
  s_state.boostPressure = s_state.spool;
  s_state.previousThrottle = throttle;
  const bool nativeBoost =
      Config::AudioEnabled && Config::AudioTurboSounds &&
      Config::AudioNativeLayers && throttle > 0.16f &&
      s_state.boostPressure > 0.18f;
  if (nativeBoost != s_state.nativeBoostActive) {
    AUDIO::SET_VEHICLE_BOOST_ACTIVE(vehicle,
                                    nativeBoost ? TRUE : FALSE);
    s_state.nativeBoostActive = nativeBoost;
  }
  
  (void)data;
  return 1.0f + (s_state.boostPressure * 0.35f);
}

float GetBoostPressure() { return s_state.boostPressure; }
bool HasTurbo() { return s_state.hasTurbo; }

} // namespace TurboSystem
