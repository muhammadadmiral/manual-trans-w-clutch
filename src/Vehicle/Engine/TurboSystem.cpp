// =============================================================================
// TurboSystem.cpp
// Turbo simulation logic. ModKit id 18 is the Turbo in GTA V.
// =============================================================================
#include "TurboSystem.h"
#include "../VehicleData.h"
#include "../../Core/Config.h"
#include "../../../sdk/inc/natives.h"
#include <algorithm>

namespace TurboSystem {

static TurboState s_state;

static inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void Reset() {
  s_state = TurboState{};
}

void InitializeForVehicle(Vehicle vehicle) {
  // Check if turbo is installed (mod category 18)
  s_state.hasTurbo = VEHICLE::IS_TOGGLE_MOD_ON(vehicle, 18) != FALSE;
}

float Update(Vehicle vehicle, VehicleData &data, float rpm, float throttle, bool isEngineOn) {
  if (!s_state.hasTurbo || !isEngineOn) {
    s_state.spool = 0.0f;
    s_state.boostPressure = 0.0f;
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
  } else {
    float diff = s_state.spool - targetSpool;
    if (diff > 0.3f && throttle < 0.1f && !s_state.blowOffLatched) {
      if (Config::AudioNativeLayers)
      AUDIO::PLAY_SOUND_FROM_ENTITY(-1, "TURBO_BLOW_OFF", vehicle, "0", 0, 0);
      s_state.blowOffLatched = true;
    }
    s_state.spool -= dt * (throttle < 0.1f ? 2.8f : 1.2f);
    if (s_state.spool < targetSpool) s_state.spool = targetSpool;
  }
  
  s_state.boostPressure = s_state.spool;
  
  (void)data;
  return 1.0f + (s_state.boostPressure * 0.35f);
}

float GetBoostPressure() { return s_state.boostPressure; }
bool HasTurbo() { return s_state.hasTurbo; }

} // namespace TurboSystem
