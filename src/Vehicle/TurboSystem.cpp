// =============================================================================
// TurboSystem.cpp
// Turbo simulation logic. ModKit id 18 is the Turbo in GTA V.
// =============================================================================
#include "TurboSystem.h"
#include "VehicleData.h"
#include "../../sdk/inc/natives.h"

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

  // Exhaust flow is proportional to RPM * Throttle
  float exhaustVolume = (rpm * 0.4f) + (throttle * 0.6f);
  
  // Target spool is determined by exhaust volume. A turbo needs enough exhaust 
  // to spool (e.g. above 0.3 load)
  float targetSpool = 0.0f;
  if (exhaustVolume > 0.3f) {
    targetSpool = Clamp01((exhaustVolume - 0.3f) / 0.7f);
  }
  
  // Spool up is relatively slow (lag), spool down is fast (blow-off)
  if (targetSpool > s_state.spool) {
    s_state.spool += 0.02f; // Lag
    if (s_state.spool > targetSpool) s_state.spool = targetSpool;
  } else {
    // If throttle is cut quickly, we blow off pressure rapidly
    float diff = s_state.spool - targetSpool;
    if (diff > 0.3f && throttle < 0.1f) {
      // Sudden throttle lift = blow off valve triggers
      AUDIO::PLAY_SOUND_FROM_ENTITY(-1, "TURBO_BLOW_OFF", vehicle, "0", 0, 0);
      s_state.spool *= 0.5f; // lose 50% pressure instantly
    }
    s_state.spool -= 0.05f;
    if (s_state.spool < targetSpool) s_state.spool = targetSpool;
  }
  
  s_state.boostPressure = s_state.spool;
  
  // Map boost pressure to a power multiplier (up to 1.35x power at max boost)
  return 1.0f + (s_state.boostPressure * 0.35f);
}

float GetBoostPressure() { return s_state.boostPressure; }
bool HasTurbo() { return s_state.hasTurbo; }

} // namespace TurboSystem
