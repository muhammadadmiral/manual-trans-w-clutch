// =============================================================================
// ParkingBrake.h
// Implements a persistent handbrake (parking brake) that holds the vehicle
// still even after the player releases the brake pedal. Engages/disengages
// with a dedicated key. Compatible with hill-hold scenarios.
// =============================================================================
#pragma once
#include <Windows.h>

using Vehicle = int;
class VehicleData;

namespace ParkingBrake {

// ─── Configuration ────────────────────────────────────────────────────────────
constexpr float kHandbrakeForce        = 1.0f;   // full handbrake injection
constexpr float kAutoReleaseSpeedKmH   = 3.0f;   // auto-release if moving this fast
constexpr float kHillHoldGradient      = 0.1f;   // min slope (m/s pitch) to activate hill-hold
constexpr int   kEngageDelayFrames     = 3;      // frames before engage takes effect (click feel)

struct ParkingBrakeState {
  bool  isEngaged     = false;
  bool  wasKeyDown    = false;
  bool  hillHoldActive = false;
  int   engageDelay   = 0;
  float lastSpeed     = 0.0f;
};

// ─── API ──────────────────────────────────────────────────────────────────────

// Reset state on vehicle change.
void Reset();

// Called every frame. Returns true if parking brake is currently active
// (so GearLogic can block throttle while parked).
bool Update(Vehicle vehicle, VehicleData& data, float speedKmH,
            float throttle, int manualGear, bool isEngineOn);

// Force-release (e.g., when vehicle is destroyed or player exits).
void ForceRelease(Vehicle vehicle);

void SetKey(int vk);
int  GetKey();
bool IsEngaged();
bool IsHillHoldActive();

} // namespace ParkingBrake
