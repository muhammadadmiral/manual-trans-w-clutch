// Ambil pedal GTA apa adanya; smoothing custom cuma dipakai buat clutch digital.
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
    const bool keyboardThrottle = keyDown(0x57) || keyDown(VK_UP);
    const float nativeThrottle = Clamp01(PAD::GET_CONTROL_NORMAL(0, 71));
    s_throttleDown = nativeThrottle > 0.001f || keyboardThrottle;
    s_smoothedThrottle =
        keyboardThrottle ? 1.0f : nativeThrottle;

    // ── Brake: S or DOWN ──────────────────────────────────────────────────────
    const bool keyboardBrake = keyDown(0x53) || keyDown(VK_DOWN);
    const float nativeBrake = Clamp01(PAD::GET_CONTROL_NORMAL(0, 72));
    s_brakeDown = nativeBrake > 0.001f || keyboardBrake;
    s_smoothedBrake = keyboardBrake ? 1.0f : nativeBrake;

    // ── Clutch ────────────────────────────────────────────────────────────────
    s_clutchDown = keyDown(Config::KeyClutch);
    // Natural logic: pressed = 1.0 (clutch pedal down), released = 0.0
    const float clutchTarget = s_clutchDown ? 1.0f : 0.0f;
    s_smoothedClutch = ExpSmooth(
        clutchTarget, s_smoothedClutch,
        Config::ClutchAttack,
        Config::ClutchRelease,
        dtSec);

    // ── Steering: A/D or LEFT/RIGHT ───────────────────────────────────────────
    s_rawSteerTarget = ClampSym(PAD::GET_CONTROL_NORMAL(0, 59));
    s_smoothedSteer = s_rawSteerTarget;
}

// =============================================================================
// ApplyGameControls
// =============================================================================
void ApplyGameControls(int manualGear, float clutch, float driveThrottle,
                       float driveBrake,
                       int /*maxGear*/,
                       float forwardSpeed)
{
    const float finalThrottle = Clamp01(driveThrottle);
    const float finalBrake = Clamp01(driveBrake);
    const bool hardDisconnect = IsClutchDown() || clutch >= 0.98f;
    const float clutchCoupling =
        hardDisconnect ? 0.0f : (1.0f - Clamp01(clutch));
    s_driveCoupling = manualGear == 0 ? 0.0f : clutchCoupling;

    // Reverse doang yang perlu tuker axis. Clutch tetap ngurus torque sendiri.
    if (manualGear == -1) {
        PAD::DISABLE_CONTROL_ACTION(0, 71, true);
        PAD::DISABLE_CONTROL_ACTION(0, 72, true);
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 72, finalThrottle);
        if (finalBrake > 0.02f) {
            // Di reverse, axis accelerate GTA jadi rem lawan arah.
            PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 71, finalBrake);
        }
    } else if (std::fabs(finalThrottle - s_smoothedThrottle) > 0.005f) {
        PAD::DISABLE_CONTROL_ACTION(0, 71, true);
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 71, finalThrottle);
    }
    (void)forwardSpeed;
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
    return s_smoothedThrottle;
}
float GetSmoothedBrake() {
    return s_smoothedBrake;
}
float GetSmoothedClutch() {
    return ApplyExpo(s_smoothedClutch, Config::ClutchExpo);
}
float GetSmoothedSteer() {
    return s_smoothedSteer;
}

} // namespace InputHandler
