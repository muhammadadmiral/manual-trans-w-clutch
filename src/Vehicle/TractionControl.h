// =============================================================================
// TractionControl.h
// Advanced TCS and ABS simulation. Intercepts throttle and brake inputs to
// prevent wheel spin and lockup dynamically based on wheel speed differentials.
// =============================================================================
#pragma once
#include <Windows.h>

using Vehicle = int;
class VehicleData;

namespace TractionControl {

struct TCSState {
  bool tcsEnabled = true;
  bool absEnabled = true;

  float currentSlip = 0.0f;
  float tcsActiveLevel = 0.0f; // 0.0 to 1.0 (1.0 = fully cutting power)
  float absActiveLevel = 0.0f; // 0.0 to 1.0 (1.0 = fully pulsing brakes)
  
  float lastSpeed = 0.0f;
  float wheelSpeedEstimate = 0.0f;
};

void Reset();

// Call before ApplyGameControls. Modifies finalThrottle and finalBrake in-place.
void Update(Vehicle vehicle, VehicleData &data, float speedKmH, float rpm,
            int gear, float clutch, float &finalThrottle, float &finalBrake);

void ToggleTCS();
void ToggleABS();

bool IsTCSActive();
bool IsABSActive();

} // namespace TractionControl
