#pragma once

namespace InputHandler {

void Update();
void ApplyGameControls();
void ResetEdges();

bool IsShiftUpJustPressed();
bool IsShiftDownJustPressed();
bool IsEngineJustPressed();

float GetSmoothedThrottle();
float GetSmoothedBrake();
float GetSmoothedClutch();

} // namespace InputHandler
