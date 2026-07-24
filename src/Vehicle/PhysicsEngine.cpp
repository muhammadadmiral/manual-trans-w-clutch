// =============================================================================
// PhysicsEngine.cpp  —  Full vehicle physics simulation
// Clutch bite-point, engine braking, wheel lockup, gearbox damage, judder
// =============================================================================
#include "PhysicsEngine.h"
#include "VehicleData.h"
#include "../../sdk/inc/natives.h"
#include <cmath>

namespace PhysicsEngine {

static EngineState s_state;

// ─── Helpers ─────────────────────────────────────────────────────────────────
static inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float Lerp(float a, float b, float t) { return a + (b - a) * Clamp01(t); }

// Rescale v from [a, b] to [0, 1]
static inline float InvLerp(float a, float b, float v) {
  if (b - a < 1e-6f) return 0.0f;
  return Clamp01((v - a) / (b - a));
}

// Mechanical clutch engagement curve from raw pedal value.
// raw = 0 → clutch fully open (no torque transfer)
// raw = 1 → clutch fully engaged (direct drive)
static float ComputeClutchEngagement(float rawClutch) {
  if (rawClutch < kClutchBitePoint) return 0.0f;
  if (rawClutch < kClutchBitePoint + kClutchBiteRange) {
    float t = InvLerp(kClutchBitePoint, kClutchBitePoint + kClutchBiteRange, rawClutch);
    return t * t * t; // cubic ease-in
  }
  return 1.0f;
}

// Estimate ideal RPM for a given gear at current road speed
static float ComputeIdealRPM(float speedKmH, float gearRatio, float topGearRatio) {
  if (gearRatio <= 0.0f || topGearRatio <= 0.0f) return 0.3f;
  return Clamp01((speedKmH / 300.0f) * (gearRatio / topGearRatio));
}

// ─── API ─────────────────────────────────────────────────────────────────────

void Reset() {
  s_state = EngineState{};
}

float UpdateClutch(float rawClutch, float rawThrottle, float rpm, bool isEngineOn) {
  ClutchState &cs = s_state.clutch;

  float engagement = ComputeClutchEngagement(rawClutch);

  // Apply clutch fade from overheating
  if (cs.slipHeat > kClutchFadeThreshold) {
    float fadeAmount = InvLerp(kClutchFadeThreshold, 1.0f, cs.slipHeat);
    engagement *= (1.0f - fadeAmount * 0.5f);
  }

  // Detect slipping
  cs.slipping = isEngineOn && engagement > 0.0f && engagement < 1.0f;

  // Clutch heat
  if (cs.slipping) {
    float slipSeverity = (1.0f - engagement) * (0.3f + rawThrottle * 0.7f);
    cs.slipHeat += kClutchSlipHeatRate * slipSeverity;
  } else {
    cs.slipHeat -= kClutchCoolRate;
  }
  cs.slipHeat        = Clamp01(cs.slipHeat);
  cs.engagementRatio = engagement;
  cs.prevClutchInput = rawClutch;

  return engagement;
}

bool UpdatePostGear(Vehicle vehicle, VehicleData &data, int manualGear,
                    int maxGear, float clutch, float throttle,
                    float speedKmH, bool isEngineOn,
                    int &grindTimerOut) {
  bool engineStalled = false;
  const float rpm = data.GetRPM();

  // ── 1. Engine Braking ────────────────────────────────────────────────────
  EngineBrakeState &ebs = s_state.engineBrake;
  if (manualGear > 0 && isEngineOn && clutch > 0.6f) {
    float gearRatio    = data.GetGearRatio(static_cast<uint8_t>(manualGear));
    float topGearRatio = data.GetGearRatio(static_cast<uint8_t>(maxGear));
    if (gearRatio > 0.0f && topGearRatio > 0.0f) {
      float relativeRatio = gearRatio / topGearRatio;
      float speedFactor   = (speedKmH < 80.0f ? speedKmH / 80.0f : 1.0f);
      float offThrottle   = 1.0f - throttle;
      ebs.targetForce     = offThrottle * relativeRatio * speedFactor * kEngineBrakeFactor;
    } else {
      ebs.targetForce = 0.0f;
    }
  } else {
    ebs.targetForce = 0.0f;
  }
  ebs.brakeForce = Lerp(ebs.brakeForce, ebs.targetForce, 0.15f);
  if (ebs.brakeForce > 0.02f) {
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 72, ebs.brakeForce * 0.6f);
  }

  // ── 2. Wheel Lockup ──────────────────────────────────────────────────────
  WheelState &ws = s_state.wheels;
  if (ws.lockTimer > 0) {
    --ws.lockTimer;
    if (ws.rearLocked) PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 76, 0.85f);
  } else {
    ws.rearLocked = ws.frontLocked = false;
  }

  if (manualGear > 0 && manualGear <= 2 &&
      speedKmH > kWheelLockupSpeedMin && clutch > 0.7f &&
      ebs.brakeForce > 0.3f && !ws.rearLocked) {
    float lockupChance = (ebs.brakeForce - 0.3f) * speedKmH / 100.0f;
    if (lockupChance > 0.5f && data.GetDriveForce() > 0.0f) {
      ws.rearLocked = true;
      ws.lockTimer  = kWheelLockFrames;
      AUDIO::PLAY_SOUND_FROM_ENTITY(-1, "TYRE_SCREECH", vehicle, "0", 0, 0);
    }
  }

  // ── 3. Over-Rev Damage ───────────────────────────────────────────────────
  if (rpm > 0.97f && isEngineOn && clutch > 0.5f && manualGear > 0) {
    s_state.overRevAccum += kOverRevDamageRate;
    if (s_state.overRevAccum > 0.1f) {
      s_state.gearbox.health -= 0.002f;
      s_state.overRevAccum    = 0.0f;
    }
  } else if (s_state.overRevAccum > 0.0f) {
    s_state.overRevAccum -= 0.0005f;
  }

  // ── 4. Gearbox Health & Slip ─────────────────────────────────────────────
  GearboxState &gs = s_state.gearbox;
  gs.health = Clamp(gs.health, 0.0f, 1.0f);
  gs.health -= 0.000001f; // very slow natural wear

  if (gs.slipCooldown > 0) --gs.slipCooldown;

  if (gs.health < kGearboxSlipThreshold && gs.slipCooldown == 0 &&
      manualGear > 0 && isEngineOn) {
    float slipProb = (kGearboxSlipThreshold - gs.health) * 0.003f;
    float noise    = std::fmod(rpm * 7919.0f, 1.0f);
    if (noise < slipProb) {
      data.SetClutch(0.0f);
      AUDIO::PLAY_SOUND_FROM_ENTITY(-1, "GEAR_CHANGE", vehicle, "0", 0, 0);
      gs.slipCooldown = 180;
      grindTimerOut   = 60;
    }
  }

  // ── 5. Clutch Bite-Point Judder & Stall ──────────────────────────────────
  ClutchState &cs = s_state.clutch;
  if (isEngineOn && manualGear > 0 && speedKmH < 5.0f &&
      cs.engagementRatio > 0.05f && cs.engagementRatio < 0.45f &&
      throttle < 0.15f && cs.slipping) {

    float judderFreq  = static_cast<float>(GetTickCount() % 120) / 120.0f;
    float judderForce = (0.45f - cs.engagementRatio) * (0.15f - throttle) * 4.0f;
    float judderRPM   = data.GetRPM() + std::sin(judderFreq * 6.2832f) * judderForce * 0.05f;
    data.SetRPM(Clamp01(judderRPM));

    if (cs.engagementRatio > 0.3f && data.GetRPM() < 0.15f && throttle < 0.05f) {
      engineStalled = true;
    }
  }

  return engineStalled;
}

void NotifyGrind() {
  s_state.gearbox.health -= kGearboxGrindDamage;
  s_state.gearbox.health  = Clamp01(s_state.gearbox.health);
  s_state.gearbox.wearAccumulator += kGearboxGrindDamage;
}

void NotifyRevMatch(float currentRPM, float targetGearRatio,
                    float currentGearRatio, float speedKmH) {
  float idealRPM      = ComputeIdealRPM(speedKmH, targetGearRatio, currentGearRatio);
  float diff          = currentRPM < idealRPM ? idealRPM - currentRPM : currentRPM - idealRPM;
  s_state.revMatched  = diff < 0.15f;
  s_state.revMatchRpm = idealRPM;
}

bool  IsGearboxSeized()       { return s_state.gearbox.health <= 0.0f; }
float GetGearboxHealth()      { return s_state.gearbox.health; }
float GetClutchHeat()         { return s_state.clutch.slipHeat; }
bool  AreRearWheelsLocked()   { return s_state.wheels.rearLocked; }
float GetEngineBrakeForce()   { return s_state.engineBrake.brakeForce; }
const EngineState &GetState() { return s_state; }

} // namespace PhysicsEngine
