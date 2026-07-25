// =============================================================================
// InputHandler.h
// =============================================================================
#pragma once

namespace InputHandler {

// ── Frame update ─────────────────────────────────────────────────────────────
void Update();

// Apply computed control values to the GTA V input system.
void ApplyGameControls(int manualGear, float clutch, float driveThrottle,
                       float driveBrake, int maxGear,
                       float forwardSpeed);

// Reset edge-detect state (call when player exits vehicle)
void ResetEdges();

// ── Digital edge events ───────────────────────────────────────────────────────
bool IsShiftUpJustPressed();
bool IsShiftDownJustPressed();
bool IsEngineJustPressed();
bool IsSignalLeftJustPressed();
bool IsSignalRightJustPressed();
bool IsSignalHazardJustPressed(); // Both signals (hazard)

// ── Analog smoothed values (0-1, or -1 to +1 for steer) ─────────────────────
float GetSmoothedThrottle();
float GetSmoothedBrake();
float GetSmoothedClutch();
float GetSmoothedSteer();        // -1.0 (left) to +1.0 (right)
float GetRawSteer();             // Pre-smoothing steer target (-1/0/+1)
float GetDriveCoupling();        // Last torque coupling sent to the game

// ── Raw digital state ─────────────────────────────────────────────────────────
bool IsThrottleDown();
bool IsBrakeDown();
bool IsClutchDown();

} // namespace InputHandler
