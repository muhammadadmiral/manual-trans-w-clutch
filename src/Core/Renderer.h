#pragma once

namespace Renderer {

void ShowNotification(const char *message);

void DrawTextOverlay(const char *text, float x, float y, float scale = 0.42f,
                     int r = 255, int g = 255, int b = 255, int a = 255);

void DrawBar(float x, float y, float width, float height, float fraction, int r,
             int g, int b, const char *label);

void DrawGearHUD(int manualGear, int maxGear);

void DrawGrindWarning();

void DrawDebugOverlay(int manualGear, unsigned gameGear, unsigned nextGear, float rpm, float clutch, const char* srcName);

void DrawPedalsOverlay(float rpm, float clutch, float throttle, float brake);

} // namespace Renderer
