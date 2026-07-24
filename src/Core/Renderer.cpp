#include "Renderer.h"
#include "Config.h"
#include "../../sdk/inc/natives.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Renderer {

void ShowNotification(const char *message) {
    HUD::BEGIN_TEXT_COMMAND_THEFEED_POST("STRING");
    HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(message);
    HUD::END_TEXT_COMMAND_THEFEED_POST_TICKER(false, false);
}

void DrawTextOverlay(const char *text, float x, float y, float scale,
                     int r, int g, int b, int a) {
    HUD::SET_TEXT_FONT(0);
    HUD::SET_TEXT_SCALE(scale, scale);
    HUD::SET_TEXT_COLOUR(r, g, b, a);
    HUD::SET_TEXT_OUTLINE();
    HUD::BEGIN_TEXT_COMMAND_DISPLAY_TEXT("STRING");
    HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(text);
    HUD::END_TEXT_COMMAND_DISPLAY_TEXT(x, y, 0);
}

void DrawBar(float x, float y, float width, float height, float fraction, int r,
             int g, int b, const char *label) {
    fraction = (std::max)(0.0f, (std::min)(1.0f, fraction));

    GRAPHICS::DRAW_RECT(x + width * 0.5f, y + height * 0.5f, width, height, 20,
                        20, 20, 180, 0);
    if (fraction > 0.0f) {
        const float fillWidth = width * fraction;
        GRAPHICS::DRAW_RECT(x + fillWidth * 0.5f, y + height * 0.5f, fillWidth,
                            height, r, g, b, 230, 0);
    }
    DrawTextOverlay(label, x, y - 0.018f, 0.28f);
}

void DrawGearHUD(int manualGear, int maxGear) {
    // Gear Badge Container (Bottom Left Overlay)
    const float badgeX = 0.08f;
    const float badgeY = 0.82f;
    const float badgeW = 0.075f;
    const float badgeH = 0.08f;

    // Background Box
    GRAPHICS::DRAW_RECT(badgeX + badgeW * 0.5f, badgeY + badgeH * 0.5f, badgeW,
                        badgeH, 15, 18, 24, 210, 0);

    // Determine Label and Color
    char gearStr[8]{};
    int r = 255, g = 255, b = 255;
    if (manualGear == -1) {
        strcpy_s(gearStr, "R");
        r = 255; g = 80; b = 80; // Red for Reverse
    } else if (manualGear == 0) {
        strcpy_s(gearStr, "N");
        r = 255; g = 200; b = 50; // Gold for Neutral
    } else {
        sprintf_s(gearStr, "%d", manualGear);
        r = 0; g = 220; b = 255; // Cyan for Forward Gears
    }

    // Draw Big Gear Text
    DrawTextOverlay(gearStr, badgeX + 0.022f, badgeY + 0.012f, 0.95f, r, g, b, 255);
}

void DrawGrindWarning() {
    DrawTextOverlay("CLUTCH REQUIRED! (Gears Grinding)", 0.38f, 0.75f, 0.55f,
                    255, 60, 60, 255);
}

void DrawDebugOverlay(int manualGear, unsigned gameGear, unsigned nextGear, float rpm, float clutch, const char* srcName) {
    char debugText[256]{};
    sprintf_s(debugText,
              "ModGear: %d | game: %u -> %u | RPM: %.3f | Clutch: %.2f | Src: %s",
              manualGear, gameGear, nextGear, rpm, clutch, srcName);
    DrawTextOverlay(debugText, 0.02f, 0.95f, 0.38f);
}

void DrawPedalsOverlay(float rpm, float clutch, float throttle, float brake) {
    const float barX = Config::OverlayPosX;
    const float barWidth = Config::OverlayBarWidth;
    const float barHeight = Config::OverlayBarHeight;
    const float gap = barHeight + 0.006f;
    float y = Config::OverlayPosY;

    DrawBar(barX, y, barWidth, barHeight, rpm, 220, 60, 60, "RPM");
    y += gap;
    DrawBar(barX, y, barWidth, barHeight, clutch, 220, 180, 60, "CLUTCH");
    y += gap;
    DrawBar(barX, y, barWidth, barHeight, throttle, 80, 200, 90, "THROTTLE");
    y += gap;
    DrawBar(barX, y, barWidth, barHeight, brake, 220, 90, 220, "BRAKE");
}

} // namespace Renderer
