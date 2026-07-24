// =============================================================================
// PhysicsEngine.h
// Simulates realistic vehicle dynamics: clutch bite point, wheel lockup,
// engine braking torque, rev-match detection, and transmission damage.
// This module reads directly from GameMemoryEngine's CVehicle wrappers.
// =============================================================================
#pragma once
#include <cstdint>
#include <Windows.h>

// Forward declarations
class VehicleData;
using Vehicle = int;

namespace PhysicsEngine {

// ─── Transmission Damage ─────────────────────────────────────────────────────
// Health is 0.0 (destroyed) to 1.0 (perfect). Gear grinding degrades it.
// Below kSlipThreshold, gears may pop into neutral. At 0.0 the box is seized.
struct GearboxState {
  float health = 1.0f;       // 1.0 = perfect, 0.0 = seized
  float wearAccumulator = 0.0f; // accumulates damage before committing
  int   slipCooldown = 0;    // frames until next possible slip event
};

// ─── Clutch Simulation ───────────────────────────────────────────────────────
// Tracks the mechanical bite-point engagement curve.
struct ClutchState {
  float engagementRatio = 0.0f; // 0 = fully open (no torque), 1 = fully locked
  float prevClutchInput = 0.0f;
  bool  slipping = false;       // true while clutch plate is slipping (heat)
  float slipHeat = 0.0f;        // 0.0-1.0; excessive heat = clutch fade
};

// ─── Wheel Lockup ────────────────────────────────────────────────────────────
struct WheelState {
  bool rearLocked = false;
  bool frontLocked = false;
  int  lockTimer = 0; // frames remaining in locked state
};

// ─── Engine Braking ──────────────────────────────────────────────────────────
struct EngineBrakeState {
  float brakeForce = 0.0f;   // 0-1 applied engine braking force
  float targetForce = 0.0f;
};

// ─── Main Engine State ───────────────────────────────────────────────────────
// Per-session state, reset on vehicle change.
struct EngineState {
  GearboxState   gearbox;
  ClutchState    clutch;
  WheelState     wheels;
  EngineBrakeState engineBrake;

  float idleRPM = 0.2f;      // idle RPM fraction (resolved after first tick)
  bool  revMatched = false;  // was rev-match input detected this shift?
  float revMatchRpm = 0.0f;  // target RPM for rev match

  // Over-rev damage accumulator
  float overRevAccum = 0.0f;
};

// ─── Configuration knobs (mirror ini settings) ───────────────────────────────
constexpr float kClutchBitePoint       = 0.18f; // clutch engagement starts here
constexpr float kClutchBiteRange       = 0.25f; // full engagement by 0.43f
constexpr float kClutchSlipHeatRate    = 0.004f;
constexpr float kClutchCoolRate        = 0.002f;
constexpr float kClutchFadeThreshold   = 0.85f; // heat above this = fade
constexpr float kGearboxGrindDamage    = 0.04f; // per grind event
constexpr float kGearboxSlipThreshold  = 0.35f; // health below = random slip
constexpr float kWheelLockupSpeedMin   = 20.0f; // km/h below which no lockup
constexpr float kEngineBrakeFactor     = 0.45f; // how aggressive engine braking is
constexpr float kOverRevDamageRate     = 0.001f;// per frame above redline
constexpr int   kWheelLockFrames       = 60;    // lockup duration in frames

// ─── API ─────────────────────────────────────────────────────────────────────

// Reset all physics state (call when entering a new vehicle).
void Reset();

// Called every frame before GearLogic::Update.
// Returns the modified clutch value after bite-point simulation.
float UpdateClutch(float rawClutch, float rawThrottle, float rpm,
                   bool isEngineOn);

// Called every frame after GearLogic. Applies engine braking, wheel lockup,
// and checks for over-rev damage. Returns whether engine stalled.
bool UpdatePostGear(Vehicle vehicle, VehicleData& data, int manualGear,
                    int maxGear, float clutch, float throttle,
                    float speedKmH, bool isEngineOn,
                    int& grindTimerOut);

// Notify physics engine that a gear grind event just occurred.
void NotifyGrind();

// Notify physics engine that the player attempted rev-matching on downshift.
void NotifyRevMatch(float currentRPM, float targetGearRatio,
                    float currentGearRatio, float speedKmH);

// Returns true if transmission health is so low the car is stuck in neutral.
bool IsGearboxSeized();

// Returns 0.0-1.0 gearbox health for overlay.
float GetGearboxHealth();

// Returns current clutch slip heat 0-1 for overlay.
float GetClutchHeat();

// Returns true if rear wheels are currently locked.
bool AreRearWheelsLocked();

// Returns current engine braking force 0-1 for overlay.
float GetEngineBrakeForce();

// Access the full state for debug overlay.
const EngineState& GetState();

} // namespace PhysicsEngine
