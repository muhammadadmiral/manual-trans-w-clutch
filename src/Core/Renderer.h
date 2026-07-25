#pragma once

namespace Renderer {

void ShowNotification(const char *message);

void DrawTextOverlay(const char *text, float x, float y, float scale = 0.42f,
                     int r = 255, int g = 255, int b = 255, int a = 255,
                     int font = 0, bool outline = true, bool center = false);

void DrawBar(float x, float y, float width, float height, float fraction, int r,
             int g, int b, const char *label);

void DrawGearHUD(int manualGear, int maxGear, int activeSignal, bool isEngineOn);

void DrawGrindWarning();

void DrawDebugOverlay(int manualGear, unsigned gameGear, unsigned nextGear,
                      float rpm, float clutch, const char *srcName);

void DrawPedalsOverlay(float rpm, float clutch, float throttle, float brake);

void DrawSimulationOverlay(float fuel, float oilTemp, float gearboxHealth,
                            float clutchHeat, bool parkingBrake,
                            bool wheelsLocked, float engineBrake);

} // namespace Renderer
