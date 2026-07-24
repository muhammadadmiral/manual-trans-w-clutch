#include "InputHandler.h"
#include "../../sdk/inc/natives.h"
#include "Config.h"
#include <Windows.h>

namespace InputHandler {

// ─── Helpers ─────────────────────────────────────────────────────────────────
static inline float Clamp01(float v) {
  return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}
static inline float ClampSym(float v) {
  return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
}

// ─── Edge-detection state
// ─────────────────────────────────────────────────────
static bool s_shiftUpWasDown = false;
static bool s_shiftDownWasDown = false;
static bool s_engineWasDown = false;
static bool s_signalLeftWasDown = false;
static bool s_signalRightWasDown = false;

static bool s_shiftUpJustPressed = false;
static bool s_shiftDownJustPressed = false;
static bool s_engineJustPressed = false;
static bool s_signalLeftJustPressed = false;
static bool s_signalRightJustPressed = false;

// ─── Smoothed analog values
// ───────────────────────────────────────────────────
static float s_smoothedThrottle = 0.0f;
static float s_smoothedBrake = 0.0f;
static float s_smoothedClutch = 0.0f;
static float s_smoothedSteer = 0.0f;

// ─── Expo Curve
// ─────────────────────────────────────────────────────────────── expo 0 =
// linear, 1 = full cubic. Preserves sign.
static float ApplyExpo(float raw, float expo) {
  if (expo <= 0.0f)
    return raw;
  float sign = raw < 0.0f ? -1.0f : 1.0f;
  float a = raw < 0.0f ? -raw : raw;
  // blend linear and cubic
  float curved = a * (1.0f - expo) + a * a * a * expo;
  return sign * Clamp01(curved);
}

// ─── Deadzone ────────────────────────────────────────────────────────────────
static float ApplyDeadzone(float raw, float dz) {
  if (dz <= 0.0f)
    return raw;
  float sign = raw < 0.0f ? -1.0f : 1.0f;
  float a = raw < 0.0f ? -raw : raw;
  if (a < dz)
    return 0.0f;
  return sign * (a - dz) / (1.0f - dz);
}

// ─── Smooth Axis ─────────────────────────────────────────────────────────────
static float SmoothAxis(float target, float cur, float attack, float release) {
  if (target > cur) {
    cur += attack;
    if (cur > target)
      cur = target;
  } else {
    cur -= release;
    if (cur < target)
      cur = target;
  }
  return Clamp01(cur);
}

// ─── Update
// ───────────────────────────────────────────────────────────────────
void Update() {
  const bool isUp = (GetAsyncKeyState(Config::KeyShiftUp) & 0x8000) != 0;
  const bool isDown = (GetAsyncKeyState(Config::KeyShiftDown) & 0x8000) != 0;
  const bool isEngine = (GetAsyncKeyState(Config::KeyEngine) & 0x8000) != 0;
  const bool isSignalLeft =
      (GetAsyncKeyState(Config::KeySignalLeft) & 0x8000) != 0;
  const bool isSignalRight =
      (GetAsyncKeyState(Config::KeySignalRight) & 0x8000) != 0;

  s_shiftUpJustPressed = isUp && !s_shiftUpWasDown;
  s_shiftDownJustPressed = isDown && !s_shiftDownWasDown;
  s_engineJustPressed = isEngine && !s_engineWasDown;
  s_signalLeftJustPressed = isSignalLeft && !s_signalLeftWasDown;
  s_signalRightJustPressed = isSignalRight && !s_signalRightWasDown;

  s_shiftUpWasDown = isUp;
  s_shiftDownWasDown = isDown;
  s_engineWasDown = isEngine;
  s_signalLeftWasDown = isSignalLeft;
  s_signalRightWasDown = isSignalRight;

  // Throttle (W / UP)
  const bool isThrottle = (GetAsyncKeyState(0x57) & 0x8000) != 0 ||
                          (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
  s_smoothedThrottle =
      SmoothAxis(isThrottle ? 1.0f : 0.0f, s_smoothedThrottle,
                 Config::ThrottleAttack, Config::ThrottleRelease);

  // Brake (S / DOWN)
  const bool isBrake = (GetAsyncKeyState(0x53) & 0x8000) != 0 ||
                       (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
  s_smoothedBrake = SmoothAxis(isBrake ? 1.0f : 0.0f, s_smoothedBrake,
                               Config::BrakeAttack, Config::BrakeRelease);

  // Clutch
  const bool isClutch = (GetAsyncKeyState(Config::KeyClutch) & 0x8000) != 0;
  s_smoothedClutch = SmoothAxis(isClutch ? 1.0f : 0.0f, s_smoothedClutch,
                                Config::ClutchAttack, Config::ClutchRelease);

  // Steering (A/D or LEFT/RIGHT) with expo + deadzone + smooth return
  const bool isLeft = (GetAsyncKeyState(0x41) & 0x8000) != 0 ||
                      (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
  const bool isRight = (GetAsyncKeyState(0x44) & 0x8000) != 0 ||
                       (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;

  float steerTarget = 0.0f;
  if (isLeft && !isRight)
    steerTarget = -1.0f;
  else if (isRight && !isLeft)
    steerTarget = 1.0f;

  if (steerTarget != 0.0f) {
    s_smoothedSteer += Config::SteerAttack * steerTarget;
    s_smoothedSteer = ClampSym(s_smoothedSteer);
  } else {
    if (s_smoothedSteer > 0.0f) {
      s_smoothedSteer -= Config::SteerRelease;
      if (s_smoothedSteer < 0.0f)
        s_smoothedSteer = 0.0f;
    } else if (s_smoothedSteer < 0.0f) {
      s_smoothedSteer += Config::SteerRelease;
      if (s_smoothedSteer > 0.0f)
        s_smoothedSteer = 0.0f;
    }
  }
}

// ─── ApplyGameControls
// ────────────────────────────────────────────────────────
void ApplyGameControls(int manualGear, float clutch, float rpm, int /*maxGear*/,
                       float forwardSpeed) {
  float finalThrottle = GetSmoothedThrottle();
  float finalBrake = GetSmoothedBrake();

  // Rev limiter
  if (manualGear > 0 && rpm > 0.98f)
    finalThrottle = 0.0f;

  // Steer injection
  float finalSteer = GetSmoothedSteer();
  if (finalSteer > 0.01f || finalSteer < -0.01f) {
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 0, finalSteer);
  }

  if (manualGear == -1) {
    PAD::DISABLE_CONTROL_ACTION(0, 71, true);
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 72, finalThrottle);
    if (finalBrake > 0.05f) {
      if (forwardSpeed > 0.1f)
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 72, finalBrake);
      else
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 76, finalBrake);
    }
  } else {
    if (forwardSpeed <= 0.1f)
      PAD::DISABLE_CONTROL_ACTION(0, 72, true);
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 71, finalThrottle);
    if (finalBrake > 0.05f) {
      if (forwardSpeed > 0.1f)
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 72, finalBrake);
      else if (forwardSpeed < -0.1f)
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 71, finalBrake);
      else
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 76, finalBrake);
    }
  }
}

void ResetEdges() {
  s_shiftUpWasDown = s_shiftDownWasDown = s_engineWasDown =
      s_signalLeftWasDown = s_signalRightWasDown = false;
}

bool IsShiftUpJustPressed() { return s_shiftUpJustPressed; }
bool IsShiftDownJustPressed() { return s_shiftDownJustPressed; }
bool IsEngineJustPressed() { return s_engineJustPressed; }
bool IsSignalLeftJustPressed() { return s_signalLeftJustPressed; }
bool IsSignalRightJustPressed() { return s_signalRightJustPressed; }

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
  float steerP = ApplyDeadzone(s_smoothedSteer, Config::SteerDeadzonePct);
  steerP = ApplyExpo(steerP, Config::SteerExpo);
  return ClampSym(steerP);
}

} // namespace InputHandler