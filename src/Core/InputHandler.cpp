#include "InputHandler.h"
#include "../../sdk/inc/main.h"
#include "../../sdk/inc/natives.h"
#include "Config.h"
#include <Windows.h>
#include <algorithm>

namespace InputHandler {

static bool s_shiftUpWasDown = false;
static bool s_shiftDownWasDown = false;
static bool s_engineWasDown = false;

static bool s_shiftUpJustPressed = false;
static bool s_shiftDownJustPressed = false;
static bool s_engineJustPressed = false;

static float s_smoothedThrottle = 0.0f;
static float s_smoothedBrake = 0.0f;
static float s_smoothedClutch = 0.0f;

void Update() {
  // Edge detection for digital inputs
  const bool isUp = (GetAsyncKeyState(Config::KeyShiftUp) & 0x8000) != 0;
  const bool isDown = (GetAsyncKeyState(Config::KeyShiftDown) & 0x8000) != 0;
  const bool isEngine = (GetAsyncKeyState(Config::KeyEngine) & 0x8000) != 0;

  s_shiftUpJustPressed = isUp && !s_shiftUpWasDown;
  s_shiftDownJustPressed = isDown && !s_shiftDownWasDown;
  s_engineJustPressed = isEngine && !s_engineWasDown;

  s_shiftUpWasDown = isUp;
  s_shiftDownWasDown = isDown;
  s_engineWasDown = isEngine;

  // Analog smoothing for throttle, brake, clutch
  const bool isThrottle = (GetAsyncKeyState(0x57) & 0x8000) != 0 ||
                          (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
  const bool isBrake = (GetAsyncKeyState(0x53) & 0x8000) != 0 ||
                       (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
  const bool isClutch = (GetAsyncKeyState(Config::KeyClutch) & 0x8000) != 0;

  if (isThrottle) {
    s_smoothedThrottle += Config::ThrottleAttack;
  } else {
    s_smoothedThrottle -= Config::ThrottleRelease;
  }

  if (isBrake) {
    s_smoothedBrake += Config::BrakeAttack;
  } else {
    s_smoothedBrake -= Config::BrakeRelease;
  }

  if (isClutch) {
    s_smoothedClutch += Config::ClutchAttack;
  } else {
    s_smoothedClutch -= Config::ClutchRelease;
  }

  s_smoothedThrottle = (std::max)(0.0f, (std::min)(1.0f, s_smoothedThrottle));
  s_smoothedBrake = (std::max)(0.0f, (std::min)(1.0f, s_smoothedBrake));
  s_smoothedClutch = (std::max)(0.0f, (std::min)(1.0f, s_smoothedClutch));
}

void ApplyGameControls(int manualGear, float clutch, float rpm, int maxGear,
                       float forwardSpeed) {
  float finalThrottle = s_smoothedThrottle;
  float finalBrake = s_smoothedBrake;

  // We no longer cut throttle for clutch/neutral. We rely entirely on the
  // memory writes to fClutch! This allows the engine to rev naturally when W is
  // pressed.

  // Rev Limiter Bounce: cut throttle at redline to prevent auto-upshifts.
  if (manualGear > 0 && rpm > 0.98f) {
    finalThrottle = 0.0f;
  }

  if (manualGear == -1) {
    // Reverse Gear
    // Disable native forward acceleration (71) so W doesn't fight us
    PAD::DISABLE_CONTROL_ACTION(0, 71, true);
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 72, finalThrottle);

    if (finalBrake > 0.05f) {
      if (forwardSpeed > 0.1f) {
        // Moving forwards, 72 brakes.
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 72, finalBrake);
      } else {
        // Moving backwards or stopped. 71 is disabled, use Handbrake.
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 76, finalBrake);
      }
    }
  } else {
    // Forward Gears (1-6) or Neutral (0)
    if (forwardSpeed <= 0.1f) {
      // Prevent native reverse when stopped or moving backwards
      PAD::DISABLE_CONTROL_ACTION(0, 72, true);
    }

    PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 71, finalThrottle);

    if (finalBrake > 0.05f) {
      if (forwardSpeed > 0.1f) {
        // Moving forwards, 72 brakes. (Not disabled)
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 72, finalBrake);
      } else if (forwardSpeed < -0.1f) {
        // Moving backwards, 71 brakes.
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 71, finalBrake);
      } else {
        // Stopped. Lock wheels with handbrake.
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 76, finalBrake);
      }
    }
  }
}

void ResetEdges() {
  s_shiftUpWasDown = false;
  s_shiftDownWasDown = false;
  s_engineWasDown = false;
}

bool IsShiftUpJustPressed() { return s_shiftUpJustPressed; }
bool IsShiftDownJustPressed() { return s_shiftDownJustPressed; }
bool IsEngineJustPressed() { return s_engineJustPressed; }

float GetSmoothedThrottle() { return s_smoothedThrottle; }
float GetSmoothedBrake() { return s_smoothedBrake; }
float GetSmoothedClutch() { return s_smoothedClutch; }

} // namespace InputHandler
