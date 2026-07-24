// =============================================================================
// TractionControl.cpp
// Full TCS and ABS implementation. Analyzes wheel speed vs ground speed to 
// detect slip and dynamically modulates the inputs like a real ECU.
// =============================================================================
#include "TractionControl.h"
#include "VehicleData.h"
#include "../../sdk/inc/natives.h"
#include <cmath>
#include <Windows.h>

namespace TractionControl {

static TCSState s_state;

static inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void Reset() {
  bool tcs = s_state.tcsEnabled;
  bool abs = s_state.absEnabled;
  s_state = TCSState{};
  s_state.tcsEnabled = tcs;
  s_state.absEnabled = abs;
}

void Update(Vehicle vehicle, VehicleData &data, float speedKmH, float rpm,
            int gear, float &finalThrottle, float &finalBrake) {
            
  // ── 1. Calculate Wheel Slip (Simulated) ──────────────────────────────────
  // GTA V doesn't expose raw wheel speed easily to scripts without heavy memory 
  // pattern scanning for CWheel. We simulate it using Engine RPM, Gear Ratio, 
  // and Ground Speed.
  
  float gearRatio = data.GetGearRatio(gear > 0 ? gear : 1);
  if (gearRatio <= 0.0f) gearRatio = 1.0f;
  
  // Theoretical ground speed if wheels have 100% grip (RPM -> Speed)
  float theoreticalSpeed = (rpm * 8000.0f) / (gearRatio * 100.0f); 
  
  // Slip is the difference between theoretical wheel speed and actual ground speed
  float slip = theoreticalSpeed - speedKmH;
  
  // ── 2. Traction Control System (TCS) ─────────────────────────────────────
  if (s_state.tcsEnabled && gear > 0 && finalThrottle > 0.1f) {
    // If wheels are spinning much faster than we are moving
    if (slip > 15.0f) { 
      // Calculate how much we need to cut throttle
      float overSlip = slip - 15.0f;
      float cutAmount = Clamp01(overSlip / 30.0f); // Max cut at 45km/h diff
      
      // Modulate throttle
      finalThrottle *= (1.0f - cutAmount);
      s_state.tcsActiveLevel = cutAmount;
    } else {
      s_state.tcsActiveLevel = 0.0f;
    }
  } else {
    s_state.tcsActiveLevel = 0.0f;
  }
  
  // ── 3. Anti-lock Braking System (ABS) ────────────────────────────────────
  if (s_state.absEnabled && finalBrake > 0.3f && speedKmH > 10.0f) {
    // Deceleration rate
    float decel = s_state.lastSpeed - speedKmH;
    
    // If we are decelerating unnaturally fast (wheels locked up and sliding)
    if (decel > 2.5f) { // roughly 1.5G deceleration -> lockup threshold
      // ABS pulsing: rapid on/off modulation
      // We use the system tick count to create a high-frequency pulse (15-20Hz)
      float pulse = std::sin(static_cast<float>(GetTickCount()) * 0.15f);
      if (pulse < 0.0f) {
        finalBrake *= 0.2f; // Release brake pressure
        s_state.absActiveLevel = 1.0f;
      } else {
        s_state.absActiveLevel = 0.5f;
      }
    } else {
      s_state.absActiveLevel = 0.0f;
    }
  } else {
    s_state.absActiveLevel = 0.0f;
  }
  
  s_state.lastSpeed = speedKmH;
}

void ToggleTCS() { s_state.tcsEnabled = !s_state.tcsEnabled; }
void ToggleABS() { s_state.absEnabled = !s_state.absEnabled; }
bool IsTCSActive() { return s_state.tcsActiveLevel > 0.1f; }
bool IsABSActive() { return s_state.absActiveLevel > 0.1f; }

} // namespace TractionControl
