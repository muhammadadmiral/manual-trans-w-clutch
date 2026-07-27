#include "Renderer.h"
#include "../../sdk/inc/natives.h"
#include "Config.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <Windows.h>

namespace Renderer {
namespace {

struct SafeRect {
    float left;
    float top;
    float right;
    float bottom;
    float aspect;
};

SafeRect GetSafeRect() {
    const float safe =
        std::clamp(GRAPHICS::GET_SAFE_ZONE_SIZE(), 0.80f, 1.0f);
    const float inset = (1.0f - safe) * 0.50f + 0.012f;
    const float aspect =
        std::clamp(GRAPHICS::GET_ASPECT_RATIO(FALSE), 1.25f, 3.60f);
    return {inset, inset, 1.0f - inset, 1.0f - inset, aspect};
}

float EaseOutCubic(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    const float inverse = 1.0f - t;
    return 1.0f - inverse * inverse * inverse;
}

} // namespace

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

void DrawGearHUD(int manualGear, int maxGear, int activeSignal, bool isEngineOn,
                 bool engineStarting, int transmissionMode,
                 const char *automaticSelector) {
    const float hudScale = Config::GearHudScale;
    const float badgeW = 0.072f * hudScale;
    const float badgeH = 0.145f * hudScale;
    const SafeRect safe = GetSafeRect();
    const float badgeX =
        std::clamp(Config::GearHudPosX,
                   safe.left + badgeW * 0.5f,
                   safe.right - badgeW * 0.5f);
    const float badgeY =
        std::clamp(Config::GearHudPosY,
                   safe.top + badgeH * 0.5f + 0.020f,
                   safe.bottom - badgeH * 0.5f);
    const ULONGLONG tick = GetTickCount64();

    auto formatGear = [&](int gear) -> std::string {
        if (gear < 0) return "R";
        if (gear == 0) return "N";
        return std::to_string(gear);
    };

    std::string label;
    int r = 255, g = 255, b = 255;
    if (engineStarting) {
        label = "START";
        r = 255; g = 190; b = 70;
    } else if (!isEngineOn) {
        label = "OFF";
        r = 120; g = 120; b = 120;
    } else if (transmissionMode == 1 && automaticSelector) {
        const char selector = automaticSelector[0];
        if (selector == 'D' || selector == 'S')
            label = std::string(automaticSelector) + std::to_string(manualGear);
        else
            label = automaticSelector;
        if (selector == 'R') {
            r = 255; g = 70; b = 70;
        } else if (selector == 'S' || selector == 'L') {
            r = 255; g = 155; b = 45;
        } else if (selector == 'N') {
            r = 255; g = 190; b = 60;
        } else {
            r = 70; g = 205; b = 255;
        }
    } else {
        label = formatGear(manualGear);
        if (manualGear < 0) {
            r = 255; g = 70; b = 70;
        } else if (manualGear == 0) {
            r = 255; g = 190; b = 60;
        } else if (manualGear == maxGear) {
            r = 255; g = 225; b = 70;
        } else {
            r = 70; g = 205; b = 255;
        }
    }

    static std::string currentLabel;
    static std::string previousLabel;
    static ULONGLONG changeTick = 0;
    if (label != currentLabel) {
        previousLabel = currentLabel;
        currentLabel = label;
        changeTick = tick;
    }
    const float anim =
        std::clamp(static_cast<float>(tick - changeTick) / 230.0f, 0.0f, 1.0f);
    const float eased = 1.0f - (1.0f - anim) * (1.0f - anim);

    const float pulse = anim < 1.0f ? 1.0f - anim : 0.0f;
    GRAPHICS::DRAW_RECT(badgeX + 0.002f, badgeY + 0.004f,
                        badgeW + 0.004f, badgeH + 0.005f,
                        0, 0, 0, 115, 0);
    GRAPHICS::DRAW_RECT(badgeX, badgeY,
                        badgeW + pulse * 0.007f,
                        badgeH + pulse * 0.007f,
                        10, 13, 18,
                        static_cast<int>(210 + pulse * 25.0f), 0);
    GRAPHICS::DRAW_RECT(badgeX, badgeY - badgeH * 0.5f + 0.002f,
                        badgeW, 0.004f, r, g, b, 240, 0);
    DrawTextOverlay(transmissionMode == 1 ? "AUTO" : "GEAR",
                    badgeX, badgeY - 0.068f * hudScale, 0.24f * hudScale,
                    145, 155, 170, 230, 0, false, true);

    if (!previousLabel.empty() && anim < 1.0f) {
        const int alpha = static_cast<int>((1.0f - anim) * 190.0f);
        DrawTextOverlay(previousLabel.c_str(), badgeX,
                        badgeY - 0.014f * hudScale -
                            eased * 0.035f * hudScale,
                        (0.70f - eased * 0.22f) * hudScale,
                        r, g, b, alpha, 2, false, true);
    }
    DrawTextOverlay(currentLabel.c_str(), badgeX,
                    badgeY - 0.014f * hudScale +
                        (1.0f - eased) * 0.032f * hudScale,
                    (0.76f + eased * 0.18f) * hudScale,
                    r, g, b, static_cast<int>(120 + eased * 135),
                    2, true, true);

    if (isEngineOn && !engineStarting && transmissionMode != 1) {
        const int above = (std::min)(maxGear, manualGear + 1);
        const int below = (std::max)(-1, manualGear - 1);
        if (above != manualGear) {
            const std::string text = formatGear(above);
            DrawTextOverlay(text.c_str(), badgeX,
                            badgeY - 0.050f * hudScale,
                            0.38f * hudScale, 150, 160, 175, 75,
                            2, false, true);
        }
        if (below != manualGear) {
            const std::string text = formatGear(below);
            DrawTextOverlay(text.c_str(), badgeX,
                            badgeY + 0.026f * hudScale,
                            0.38f * hudScale, 150, 160, 175, 75,
                            2, false, true);
        }
    }

    const bool blink = (tick % 700) < 350;
    const bool leftOn = blink && (activeSignal == 1 || activeSignal == 3);
    const bool rightOn = blink && (activeSignal == 2 || activeSignal == 3);
    DrawTextOverlay("<", badgeX - 0.022f * hudScale,
                    badgeY + 0.049f * hudScale, 0.40f * hudScale,
                    leftOn ? 60 : 55, leftOn ? 255 : 65,
                    leftOn ? 70 : 55, leftOn ? 255 : 100,
                    0, false, true);
    DrawTextOverlay(">", badgeX + 0.022f * hudScale,
                    badgeY + 0.049f * hudScale, 0.40f * hudScale,
                    rightOn ? 60 : 55, rightOn ? 255 : 65,
                    rightOn ? 70 : 55, rightOn ? 255 : 100,
                    0, false, true);
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
    // Disabled debug overlay per user request
}

void DrawPedalsOverlay(float rpm, float clutch, float throttle, float brake) {
    const SafeRect safe = GetSafeRect();
    const float barWidth =
        std::clamp(Config::OverlayBarWidth, 0.075f, 0.20f);
    const float barHeight = Config::OverlayBarHeight;
    const float gap = barHeight + 0.02f;
    const float barX =
        std::clamp(Config::OverlayPosX, safe.left,
                   safe.right - barWidth);
    float y = std::clamp(Config::OverlayPosY, safe.top + 0.018f,
                         safe.bottom - gap * 4.0f);

    DrawBar(barX, y, barWidth, barHeight, rpm, 255, 60, 60, "RPM");
    y += gap;
    if (clutch >= 0.0f) {
        DrawBar(barX, y, barWidth, barHeight, clutch, 60, 200, 255, "CLUTCH");
        y += gap;
    }
    DrawBar(barX, y, barWidth, barHeight, throttle, 60, 255, 100, "THROTTLE");
    y += gap;
    DrawBar(barX, y, barWidth, barHeight, brake, 255, 100, 100, "BRAKE");
}

void DrawSimulationOverlay(float fuel, float oilTemp, float oilLife,
                           float gearboxHealth,
                           float clutchHeat, bool parkingBrake,
                           bool wheelsLocked, float engineBrake) {
    const SafeRect safe = GetSafeRect();
    const float barWidth =
        std::clamp(Config::OverlayBarWidth, 0.075f, 0.20f);
    const float barHeight = Config::OverlayBarHeight;
    const float gap = barHeight + 0.02f;
    const float pedalX =
        std::clamp(Config::OverlayPosX, safe.left,
                   safe.right - barWidth);
    float barX = pedalX + barWidth + 0.015f;
    float y = std::clamp(Config::OverlayPosY, safe.top + 0.018f,
                         safe.bottom - gap * 7.0f);
    if (barX + barWidth > safe.right) {
        barX = pedalX;
        y = std::clamp(y + gap * 4.0f + 0.015f,
                       safe.top + 0.018f,
                       safe.bottom - gap * 7.0f);
    }

    int fR = static_cast<int>((1.0f - fuel) * 255);
    int fG = static_cast<int>(fuel * 200);
    DrawBar(barX, y, barWidth, barHeight, fuel, fR, fG, 40, "FUEL");
    y += gap;

    int otR = static_cast<int>(oilTemp * 255);
    int otB = static_cast<int>((1.0f - oilTemp) * 200);
    DrawBar(barX, y, barWidth, barHeight, oilTemp, otR, 80, otB, "OIL TEMP");
    y += gap;

    DrawBar(barX, y, barWidth, barHeight, oilLife,
            static_cast<int>((1.0f - oilLife) * 255),
            static_cast<int>(oilLife * 200), 50, "OIL LIFE");
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

void DrawSpeedometer(const SpeedometerData &data) {
    Speedometer::Draw(data);
}

void DrawInteractionPanel(const char *title, const char *detail,
                          float progress) {
    const SafeRect safe = GetSafeRect();
    const ULONGLONG now = GetTickCount64();
    static ULONGLONG lastDrawAt = 0;
    static ULONGLONG enteredAt = 0;
    if (!lastDrawAt || now - lastDrawAt > 180)
        enteredAt = now;
    lastDrawAt = now;
    const float reveal = EaseOutCubic(
        static_cast<float>(now - enteredAt) / 220.0f);

    const float x = 0.5f;
    const float width =
        std::clamp(0.29f * (16.0f / 9.0f) / safe.aspect,
                   0.225f, 0.34f);
    const float height =
        (progress >= 0.0f ? 0.078f : 0.060f) * (0.94f + reveal * 0.06f);
    const float settledY =
        (std::min)(0.82f, safe.bottom - height * 0.5f - 0.018f);
    const float y = settledY + (1.0f - reveal) * 0.025f;
    const int panelAlpha = static_cast<int>(80.0f + reveal * 145.0f);
    const int contentAlpha = static_cast<int>(80.0f + reveal * 175.0f);

    GRAPHICS::DRAW_RECT(x + 0.002f, y + 0.004f,
                        width + 0.006f, height + 0.006f,
                        0, 0, 0, static_cast<int>(reveal * 120.0f), 0);
    GRAPHICS::DRAW_RECT(x, y, width, height, 8, 12, 18, panelAlpha, 0);
    GRAPHICS::DRAW_RECT(x, y - height * 0.5f + 0.002f,
                        width, 0.004f, 55, 205, 255, contentAlpha, 0);
    DrawTextOverlay(title, x, y - 0.026f, 0.36f,
                    235, 245, 255, contentAlpha, 0, true, true);
    DrawTextOverlay(detail, x, y - 0.002f, 0.29f,
                    190, 205, 220, contentAlpha, 0, false, true);
    if (progress >= 0.0f) {
        progress = std::clamp(progress, 0.0f, 1.0f);
        const float barW = width - 0.026f;
        const float left = x - barW * 0.5f;
        const float barY = y + 0.023f;
        GRAPHICS::DRAW_RECT(x, barY, barW, 0.008f,
                            22, 30, 38, contentAlpha, 0);
        if (progress > 0.0f) {
            const float fill = barW * progress;
            GRAPHICS::DRAW_RECT(left + fill * 0.5f, barY, fill, 0.008f,
                                55, 205, 255, contentAlpha, 0);
        }
    }
}

} // namespace Renderer
