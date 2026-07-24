#include "InputHandler.h"
#include "Config.h"
#include "../../sdk/inc/main.h" 
#include "../../sdk/inc/natives.h" 
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
    const bool isThrottle = (GetAsyncKeyState(0x57) & 0x8000) != 0 || (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
    const bool isBrake = (GetAsyncKeyState(0x53) & 0x8000) != 0 || (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
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

void ApplyGameControls(int manualGear) {
    // 71 = Accelerate, 72 = Brake
    if (manualGear == -1) {
        // In Reverse: W (Throttle) makes the car go backwards -> game needs Control 72
        // S (Brake) stops the car -> game needs Control 71
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 71, s_smoothedBrake);
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 72, s_smoothedThrottle);
    } else {
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 71, s_smoothedThrottle);
        PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 72, s_smoothedBrake);
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
