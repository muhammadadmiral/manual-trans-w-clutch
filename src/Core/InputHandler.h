#pragma once

namespace InputHandler {

void Update();
void ApplyGameControls(int manualGear, float clutch, float rpm, int maxGear, float forwardSpeed);
void ResetEdges();

bool IsShiftUpJustPressed();
bool IsShiftDownJustPressed();
bool IsEngineJustPressed();

float GetSmoothedThrottle();
float GetSmoothedBrake();
float GetSmoothedClutch();

} // namespace InputHandler
