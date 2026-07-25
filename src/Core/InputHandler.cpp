// =============================================================================
// InputHandler.cpp
// Time-based analog smoothing for throttle / brake / clutch / steer.
//
// ── Why "additive per-frame" smoothing was too fast ───────────────────────────
// The old code did:
//     value += attack_constant;   // each frame
// At 60 fps, attack=0.05 → value reaches 1.0 in just 20 frames = 0.33 s.
// Even attack=0.01 still reaches 1.0 in 100 frames = 1.67 s at 60 fps, but
// at 120 fps (0.008 s/frame) the same 0.01 per frame gives 0.01×120 = 1.2/s
// — nearly instantaneous.
//
// ── New approach: exponential decay with real deltaTime ───────────────────────
// The smoothed value V is driven toward the target T as:
//     V += (T - V) × (1 - exp(-Δt / τ))
// where τ (tau) is the time constant in seconds.
//   τ = 0.05 → reaches 63 % of target in 50 ms  (very fast — clutch snap)
//   τ = 0.15 → reaches 63 % of target in 150 ms (throttle attack)
//   τ = 0.30 → reaches 63 % of target in 300 ms (throttle release — coasting)
//   τ = 0.50 → reaches 63 % of target in 500 ms (smooth braking ramp)
//
// This is framerate-independent: a 120 Hz player and a 30 Hz player feel
// exactly the same pedal response.
//
// ── Expo curve (optional shaping on top) ─────────────────────────────────────
// After smoothing, an optional cubic expo curve re-maps the value:
//     y = x × (1 - expo) + x³ × expo
// expo=0 is linear, expo=0.5 gives gentle deadband+progression,
// expo=1.0 is full cubic (slow centre, fast edges).
//
// ── Keyboard vs controller ────────────────────────────────────────────────────
// Since GTA V keyboard gives only 0/1 targets, smoothing IS the entire
// feel of the pedal. The time constants in the config INI should be tuned to
// taste — recommended starting points are in Config.cpp (e.g. τ_throttle = 0.10).
// =============================================================================
#define NOMINMAX
#include "InputHandler.h"
#include "../../sdk/inc/natives.h"
#include "Config.h"
#include <Windows.h>
#include <cmath>

namespace InputHandler {

// =============================================================================
// Helpers
// =============================================================================
static inline float Clamp01(float v)  { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float ClampSym(float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }

// Expo curve — preserves sign, blends linear with cubic.
// expo ∈ [0, 1]:  0 = linear, 1 = full cubic.
static float ApplyExpo(float raw, float expo) {
    if (expo <= 0.0f) return raw;
    const float sign = raw < 0.0f ? -1.0f : 1.0f;
    const float a    = raw < 0.0f ? -raw  : raw;
    return sign * Clamp01(a * (1.0f - expo) + a * a * a * expo);
}

// Deadzone — rescales the remaining range to [0, 1].
static float ApplyDeadzone(float raw, float dz) {
    if (dz <= 0.0f) return raw;
    const float sign = raw < 0.0f ? -1.0f : 1.0f;
    const float a    = raw < 0.0f ? -raw  : raw;
    if (a < dz) return 0.0f;
    return sign * (a - dz) / (1.0f - dz);
}

// Framerate-independent exponential smoothing.
//   tau     = time constant in seconds (63% of the way there in <tau> seconds)
//   dtSec   = elapsed time this frame in seconds
//
// Separate tau_attack / tau_release let the pedal feel asymmetric:
// fast press, slow lift-off.
static float ExpSmooth(float target, float current,
                       float tau_attack, float tau_release,
                       float dtSec)
{
    if (dtSec <= 0.0f || dtSec > 0.25f) return current; // guard for first frame / hitches
    const float tau = (target > current) ? tau_attack : tau_release;
    const float k   = (tau > 0.001f) ? (1.0f - std::expf(-dtSec / tau)) : 1.0f;
    const float v   = current + (target - current) * k;
    return Clamp01(v);
}

// Symmetric variant for steering (target in −1…+1).
static float ExpSmoothSym(float target, float current,
                           float tau_attack, float tau_release,
                           float dtSec)
{
    if (dtSec <= 0.0f || dtSec > 0.25f) return current;
    const float tau = (fabsf(target) > fabsf(current)) ? tau_attack : tau_release;
    const float k   = (tau > 0.001f) ? (1.0f - std::expf(-dtSec / tau)) : 1.0f;
    return ClampSym(current + (target - current) * k);
}

// =============================================================================
// Per-frame state
// =============================================================================

// ── Delta time ────────────────────────────────────────────────────────────────
static ULONGLONG s_lastTick = 0;

// ── Edge-detect ───────────────────────────────────────────────────────────────
static bool s_shiftUpWasDown      = false;
static bool s_shiftDownWasDown    = false;
static bool s_engineWasDown       = false;
static bool s_signalLeftWasDown   = false;
static bool s_signalRightWasDown  = false;
static bool s_signalHazardWasDown = false;

static bool s_shiftUpJustPressed      = false;
static bool s_shiftDownJustPressed    = false;
static bool s_engineJustPressed       = false;
static bool s_signalLeftJustPressed   = false;
static bool s_signalRightJustPressed  = false;
static bool s_signalHazardJustPressed = false;

// ── Smoothed analog values ────────────────────────────────────────────────────
static float s_smoothedThrottle = 0.0f;
static float s_smoothedBrake    = 0.0f;
static float s_smoothedClutch   = 0.0f;
static float s_smoothedSteer    = 0.0f;
static float s_driveCoupling    = 0.0f;

// ── Raw state ─────────────────────────────────────────────────────────────────
static bool  s_throttleDown  = false;
static bool  s_brakeDown     = false;
static bool  s_clutchDown    = false;
static float s_rawSteerTarget = 0.0f;

// =============================================================================
// Update — called every ScriptHookV frame
// =============================================================================
void Update() {
    // ── Delta time ────────────────────────────────────────────────────────────
    const ULONGLONG now = GetTickCount64();
    const float dtSec = (s_lastTick == 0)
        ? (1.0f / 60.0f)
        : std::fminf(static_cast<float>(now - s_lastTick) * 0.001f, 0.2f);
    s_lastTick = now;

    // ── Digital keys: edge detect ─────────────────────────────────────────────
    auto keyDown = [](int vk) -> bool {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    };

    const bool isUp         = keyDown(Config::KeyShiftUp);
    const bool isDown       = keyDown(Config::KeyShiftDown);
    const bool isEngine     = keyDown(Config::KeyEngine);
    const bool isSignalL    = keyDown(Config::KeySignalLeft);
    const bool isSignalR    = keyDown(Config::KeySignalRight);
    const bool isHazard     = keyDown(Config::KeySignalHazard);

    s_shiftUpJustPressed      = isUp      && !s_shiftUpWasDown;
    s_shiftDownJustPressed    = isDown    && !s_shiftDownWasDown;
    s_engineJustPressed       = isEngine  && !s_engineWasDown;
    s_signalLeftJustPressed   = isSignalL && !s_signalLeftWasDown;
    s_signalRightJustPressed  = isSignalR && !s_signalRightWasDown;
    s_signalHazardJustPressed = isHazard  && !s_signalHazardWasDown;

    s_shiftUpWasDown      = isUp;
    s_shiftDownWasDown    = isDown;
    s_engineWasDown       = isEngine;
    s_signalLeftWasDown   = isSignalL;
    s_signalRightWasDown  = isSignalR;
    s_signalHazardWasDown = isHazard;

    // ── Throttle: W or UP ──────────────────────────────────────────────────────
    s_throttleDown = keyDown(0x57) || keyDown(VK_UP);
    const float throttleTarget = s_throttleDown ? 1.0f : 0.0f;
    s_smoothedThrottle = ExpSmooth(
        throttleTarget, s_smoothedThrottle,
        Config::ThrottleAttack,   // τ attack  (s)
        Config::ThrottleRelease,  // τ release (s)
        dtSec);

    // ── Brake: S or DOWN ──────────────────────────────────────────────────────
    s_brakeDown = keyDown(0x53) || keyDown(VK_DOWN);
    const float brakeTarget = s_brakeDown ? 1.0f : 0.0f;
    s_smoothedBrake = ExpSmooth(
        brakeTarget, s_smoothedBrake,
        Config::BrakeAttack,
        Config::BrakeRelease,
        dtSec);

    // ── Clutch ────────────────────────────────────────────────────────────────
    s_clutchDown = keyDown(Config::KeyClutch);
    // Natural logic: pressed = 1.0 (clutch pedal down), released = 0.0
    const float clutchTarget = s_clutchDown ? 1.0f : 0.0f;
    s_smoothedClutch = ExpSmooth(
        clutchTarget, s_smoothedClutch,
        Config::ClutchAttack,
        std::fmaxf(Config::ClutchRelease, 0.14f),
        dtSec);

    // ── Steering: A/D or LEFT/RIGHT ───────────────────────────────────────────
    const bool isLeft  = keyDown(0x41) || keyDown(VK_LEFT);
    const bool isRight = keyDown(0x44) || keyDown(VK_RIGHT);

    s_rawSteerTarget = 0.0f;
    if (isLeft  && !isRight) s_rawSteerTarget = -1.0f;
    if (isRight && !isLeft)  s_rawSteerTarget =  1.0f;

    s_smoothedSteer = ExpSmoothSym(
        s_rawSteerTarget, s_smoothedSteer,
        Config::SteerAttack,
        Config::SteerRelease,
        dtSec);
}

// =============================================================================
// ApplyGameControls
// =============================================================================
void ApplyGameControls(int manualGear, float clutch, float driveThrottle,
                       int /*maxGear*/,
                       float forwardSpeed)
{
    const float finalThrottle = Clamp01(driveThrottle);
    float finalBrake    = GetSmoothedBrake();
    const bool hardDisconnect = IsClutchDown() || clutch >= 0.98f;
    const float clutchCoupling =
        hardDisconnect ? 0.0f : (1.0f - Clamp01(clutch));
    s_driveCoupling = manualGear == 0 ? 0.0f : clutchCoupling;

    // We removed the aggressive custom rev limiter. 
    // Since we now set TopGear to current gear, the native GTA V auto-upshift is disabled,
    // so we can let the game's natural rev limiter bounce quickly at redline.

    // Steer injection
    const float finalSteer = GetSmoothedSteer();
    if (finalSteer > 0.005f || finalSteer < -0.005f)
        // INPUT_VEH_MOVE_LR is control 59.  Control 0 is NEXT_CAMERA and was
        // the reason the camera changed while the player was only steering.
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 59, finalSteer);

    if (manualGear == 0) {
        // GearLogic keeps the native gearbox in true neutral, so throttle can
        // reach GTA's engine normally: free-rev audio and RPM now match R.
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 71, finalThrottle);
        if (finalBrake > 0.02f)
            PAD::SET_CONTROL_VALUE_NEXT_FRAME(0,
                forwardSpeed > 0.1f ? 72 : 76, finalBrake);
    } else if (manualGear == -1) {
        // Reverse gear — swap throttle/brake controls
        const float coupledThrottle = finalThrottle * clutchCoupling;
        PAD::DISABLE_CONTROL_ACTION(0, 71, true);
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 72, coupledThrottle);
        if (finalBrake > 0.02f) {
            PAD::SET_CONTROL_VALUE_NEXT_FRAME(0,
                forwardSpeed > 0.1f ? 72 : 76, finalBrake);
        }
    } else {
        // The temporary memory-neutral state is the authoritative clutch
        // disconnect. Do not disable control 71 here: on this GTA build a
        // disabled accelerator accepts no usable forward drive, which made
        // first and second gear stall while reverse still worked.
        // While disconnected, feed engine throttle for native free-rev audio;
        // once the selected gear reconnects, apply the clutch/TCS coupling.
        const float engineThrottle =
            (hardDisconnect || clutch > 0.45f) ? finalThrottle
                                                : finalThrottle * clutchCoupling;
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 71, engineThrottle);
        if (finalBrake > 0.02f) {
            if      (forwardSpeed >  0.1f) PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 72, finalBrake);
            else if (forwardSpeed < -0.1f) PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 71, finalBrake);
            else                           PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 76, finalBrake);
        }
    }
}

// =============================================================================
// Reset
// =============================================================================
void ResetEdges() {
    s_shiftUpWasDown = s_shiftDownWasDown = s_engineWasDown =
    s_signalLeftWasDown = s_signalRightWasDown = s_signalHazardWasDown = false;
    s_lastTick = 0;
    s_smoothedThrottle = s_smoothedBrake = s_smoothedClutch = 0.0f;
    s_smoothedSteer = s_rawSteerTarget = s_driveCoupling = 0.0f;
}

// =============================================================================
// Getters
// =============================================================================
bool IsShiftUpJustPressed()      { return s_shiftUpJustPressed;      }
bool IsShiftDownJustPressed()    { return s_shiftDownJustPressed;     }
bool IsEngineJustPressed()       { return s_engineJustPressed;        }
bool IsSignalLeftJustPressed()   { return s_signalLeftJustPressed;    }
bool IsSignalRightJustPressed()  { return s_signalRightJustPressed;   }
bool IsSignalHazardJustPressed() { return s_signalHazardJustPressed;  }

bool IsThrottleDown() { return s_throttleDown; }
bool IsBrakeDown()    { return s_brakeDown;     }
bool IsClutchDown()   { return s_clutchDown;    }

float GetRawSteer()        { return s_rawSteerTarget; }
float GetDriveCoupling()   { return s_driveCoupling; }

float GetSmoothedThrottle() {
    return ApplyExpo(s_smoothedThrottle, Config::ThrottleExpo);
}
float GetSmoothedBrake() {
    return ApplyExpo(s_smoothedBrake, Config::BrakeExpo);
}
float GetSmoothedClutch() {
    return ApplyExpo(s_smoothedClutch, Config::ClutchExpo);
}
float GetSmoothedSteer() {
    float s = ApplyDeadzone(s_smoothedSteer, Config::SteerDeadzonePct);
    s = ApplyExpo(s, Config::SteerExpo);
    return ClampSym(s);
}

} // namespace InputHandler
