#include "Renderer.h"
#include "../../sdk/inc/natives.h"
#include "Config.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <Windows.h>

namespace Renderer {

void ShowNotification(const char *message) {
    HUD::BEGIN_TEXT_COMMAND_THEFEED_POST("CELL_EMAIL_BCON");
    const size_t len = std::strlen(message);
    for (size_t i = 0; i < len; i += 99) {
        char chunk[100]{};
        strncpy_s(chunk, message + i, 99);
        HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(chunk);
    }
    HUD::END_TEXT_COMMAND_THEFEED_POST_TICKER(false, false);
}

void DrawTextOverlay(const char *text, float x, float y, float scale,
                     int r, int g, int b, int a,
                     int font, bool outline, bool center) {
    HUD::SET_TEXT_FONT(font);
    HUD::SET_TEXT_SCALE(scale, scale);
    HUD::SET_TEXT_COLOUR(r, g, b, a);
    if (outline) HUD::SET_TEXT_OUTLINE();
    if (center) {
        HUD::SET_TEXT_CENTRE(TRUE);
    }
    HUD::BEGIN_TEXT_COMMAND_DISPLAY_TEXT("STRING");
    HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(text);
    HUD::END_TEXT_COMMAND_DISPLAY_TEXT(x, y, 0);
}

void DrawBar(float x, float y, float width, float height, float fraction, int r,
             int g, int b, const char *label) {
    fraction = (std::max)(0.0f, (std::min)(1.0f, fraction));

    GRAPHICS::DRAW_RECT(x + width * 0.5f, y + height * 0.5f, width, height, 15, 18, 24, 200, 0);
    if (fraction > 0.0f) {
        const float fillWidth = width * fraction;
        GRAPHICS::DRAW_RECT(x + fillWidth * 0.5f, y + height * 0.5f, fillWidth,
                            height, r, g, b, 230, 0);
    }
    DrawTextOverlay(label, x, y - 0.016f, 0.28f, 255, 255, 255, 255, 0, true, false);
}

void DrawGearHUD(int manualGear, int maxGear, int activeSignal, bool isEngineOn) {
    // Positioning - Bottom right corner, sleek and minimalist
    const float badgeX = 0.88f;
    const float badgeY = 0.85f;
    const float badgeW = 0.055f;
    const float badgeH = 0.085f;

    // Dark sleek backdrop
    GRAPHICS::DRAW_RECT(badgeX, badgeY, badgeW, badgeH, 15, 15, 18, 220, 0);
    // Subtle top border/accent
    GRAPHICS::DRAW_RECT(badgeX, badgeY - (badgeH / 2.0f) + 0.002f, badgeW, 0.004f, 60, 200, 255, 255, 0);

    // Label "GEAR" (clean font)
    DrawTextOverlay("GEAR", badgeX, badgeY - 0.038f, 0.25f, 150, 150, 150, 255, 0, false, true);

    // Determine Label and Color
    char gearStr[8]{};
    int r = 255, g = 255, b = 255;
    if (!isEngineOn) {
        strcpy_s(gearStr, "OFF");
        r = 120; g = 120; b = 120;
    } else if (manualGear == -1) {
        strcpy_s(gearStr, "R");
        r = 255; g = 60; b = 60; // Red Reverse
    } else if (manualGear == 0) {
        strcpy_s(gearStr, "N");
        r = 255; g = 180; b = 40; // Orange Neutral
    } else {
        sprintf_s(gearStr, "%d", manualGear);
        // Cyan for normal, Green for cruising gears, Yellow for max gear
        if (manualGear <= 3) { r = 60; g = 200; b = 255; }
        else if (manualGear < maxGear) { r = 100; g = 255; b = 100; }
        else { r = 255; g = 220; b = 50; }
    }

    // Draw Big Gear Text (Font 2 is Chalet London, very clean, much better than 0 or 4)
    DrawTextOverlay(gearStr, badgeX, badgeY - 0.025f, 1.0f, r, g, b, 255, 2, false, true);

    // Turn Signals at the bottom
    const ULONGLONG tick = GetTickCount64();
    const bool blink = (tick % 700) < 350; // slightly faster sleek blink

    int lR = 50, lG = 50, lB = 50, lA = 120;
    int rR = 50, rG = 50, rB = 50, rA = 120;

    if (blink) {
        if (activeSignal == 1 || activeSignal == 3) { lR = 60; lG = 255; lB = 60; lA = 255; }
        if (activeSignal == 2 || activeSignal == 3) { rR = 60; rG = 255; rB = 60; rA = 255; }
    }

    // Left indicator
    DrawTextOverlay("~u~<", badgeX - 0.018f, badgeY + 0.015f, 0.45f, lR, lG, lB, lA, 0, false, true);
    // Right indicator
    DrawTextOverlay("~u~>", badgeX + 0.018f, badgeY + 0.015f, 0.45f, rR, rG, rB, rA, 0, false, true);
}

void DrawGrindWarning() {
    // Center screen flash
    const ULONGLONG tick = GetTickCount64();
    if ((tick % 400) < 200) {
        DrawTextOverlay("CLUTCH REQUIRED!", 0.5f, 0.35f, 0.8f, 255, 40, 40, 255, 2, true, true);
        DrawTextOverlay("Gears are grinding", 0.5f, 0.40f, 0.4f, 255, 100, 100, 255, 0, true, true);
    }
}

void DrawDebugOverlay(int manualGear, unsigned gameGear, unsigned nextGear,
                      float rpm, float clutch, const char *srcName) {
    char debugText[256]{};
    sprintf_s(debugText,
              "ModGear: %d | game: %u -> %u | RPM: %.3f | Clutch: %.2f | Src: %s",
              manualGear, gameGear, nextGear, rpm, clutch, srcName);
    DrawTextOverlay(debugText, 0.02f, 0.95f, 0.38f, 255, 255, 255, 255, 0, true, false);
}

void DrawPedalsOverlay(float rpm, float clutch, float throttle, float brake) {
    const float barX = Config::OverlayPosX;
    const float barWidth = Config::OverlayBarWidth;
    const float barHeight = Config::OverlayBarHeight;
    const float gap = barHeight + 0.02f;
    float y = Config::OverlayPosY;

    DrawBar(barX, y, barWidth, barHeight, rpm, 255, 60, 60, "RPM");
    y += gap;
    DrawBar(barX, y, barWidth, barHeight, clutch, 60, 200, 255, "CLUTCH");
    y += gap;
    DrawBar(barX, y, barWidth, barHeight, throttle, 60, 255, 100, "THROTTLE");
    y += gap;
    DrawBar(barX, y, barWidth, barHeight, brake, 255, 100, 100, "BRAKE");
}

void DrawSimulationOverlay(float fuel, float oilTemp, float gearboxHealth,
                           float clutchHeat, bool parkingBrake,
                           bool wheelsLocked, float engineBrake) {
    const float barX = Config::OverlayPosX + Config::OverlayBarWidth + 0.015f;
    const float barWidth = Config::OverlayBarWidth;
    const float barHeight = Config::OverlayBarHeight;
    const float gap = barHeight + 0.02f;
    float y = Config::OverlayPosY;

    int fR = static_cast<int>((1.0f - fuel) * 255);
    int fG = static_cast<int>(fuel * 200);
    DrawBar(barX, y, barWidth, barHeight, fuel, fR, fG, 40, "FUEL");
    y += gap;

    int otR = static_cast<int>(oilTemp * 255);
    int otB = static_cast<int>((1.0f - oilTemp) * 200);
    DrawBar(barX, y, barWidth, barHeight, oilTemp, otR, 80, otB, "OIL TEMP");
    y += gap;

    int ghR = static_cast<int>((1.0f - gearboxHealth) * 255);
    int ghG = static_cast<int>(gearboxHealth * 200);
    DrawBar(barX, y, barWidth, barHeight, gearboxHealth, ghR, ghG, 50, "GEARBOX");
    y += gap;

    DrawBar(barX, y, barWidth, barHeight, clutchHeat, 255,
            static_cast<int>((1.0f - clutchHeat) * 140), 40, "CLUTCH HEAT");
    y += gap;

    DrawBar(barX, y, barWidth, barHeight, engineBrake, 180, 100, 255, "ENG BRAKE");
    y += gap;

    if (parkingBrake) {
        DrawTextOverlay("~o~[P] PARKED", barX, y, 0.35f, 255, 150, 0, 255, 0, true, false);
        y += 0.03f;
    }
    if (wheelsLocked) {
        DrawTextOverlay("~r~WHEEL LOCK!", barX, y, 0.35f, 255, 50, 50, 255, 0, true, false);
    }
}

} // namespace Renderer
