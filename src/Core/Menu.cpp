#include "Menu.h"
#include "../../sdk/inc/natives.h"
#include "Config.h"
#include <vector>

bool Menu::isOpen = false;
int Menu::selectedIndex = 0;

void Menu::Toggle() {
    isOpen = !isOpen;
}

bool Menu::IsOpen() {
    return isOpen;
}

void Menu::DrawRect(float x, float y, float width, float height, int r, int g, int b, int a) {
    GRAPHICS::DRAW_RECT(x + width * 0.5f, y + height * 0.5f, width, height, r, g, b, a, 0);
}

void Menu::DrawTextStr(const std::string& text, float x, float y, float scale, int r, int g, int b, int a, bool center) {
    HUD::SET_TEXT_FONT(0);
    HUD::SET_TEXT_SCALE(scale, scale);
    HUD::SET_TEXT_COLOUR(r, g, b, a);
    HUD::SET_TEXT_OUTLINE();
    if (center) {
        HUD::SET_TEXT_CENTRE(TRUE);
    }
    HUD::BEGIN_TEXT_COMMAND_DISPLAY_TEXT("STRING");
    HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(text.c_str());
    HUD::END_TEXT_COMMAND_DISPLAY_TEXT(x, y, 0);
}

void Menu::Update() {
    if (PAD::IS_CONTROL_JUST_PRESSED(0, Config::KeyMenu) || PAD::IS_CONTROL_JUST_PRESSED(2, Config::KeyMenu)) {
        Toggle();
    }

    if (!isOpen) return;

    if (PAD::IS_CONTROL_JUST_PRESSED(0, 172)) { // UP (Frontend Up)
        selectedIndex--;
        if (selectedIndex < 0) selectedIndex = numItems - 1;
    }
    if (PAD::IS_CONTROL_JUST_PRESSED(0, 173)) { // DOWN (Frontend Down)
        selectedIndex++;
        if (selectedIndex >= numItems) selectedIndex = 0;
    }

    if (PAD::IS_CONTROL_JUST_PRESSED(0, 176)) { // ENTER (Frontend Accept)
        switch (selectedIndex) {
            case 0: Config::RequireColdStart = !Config::RequireColdStart; break;
            case 1: Config::DebugOverlay = !Config::DebugOverlay; break;
            case 2: Config::AllowQuadbikes = !Config::AllowQuadbikes; break;
            case 3: Config::UseRealClutch = !Config::UseRealClutch; break;
        }
        // TODO: Save to INI instantly
    }
}

void Menu::Draw() {
    if (!isOpen) return;

    const float menuX = 0.75f;
    const float menuY = 0.2f;
    const float menuWidth = 0.2f;
    const float itemHeight = 0.035f;
    const float headerHeight = 0.08f;

    // Header
    DrawRect(menuX, menuY, menuWidth, headerHeight, 200, 50, 50, 255);
    DrawTextStr("MANUAL TRANS", menuX + menuWidth / 2, menuY + 0.02f, 0.6f, 255, 255, 255, 255, true);

    float currentY = menuY + headerHeight;

    std::vector<std::string> itemNames = {
        "Require Cold Start: " + std::string(Config::RequireColdStart ? "ON" : "OFF"),
        "Overlay UI: " + std::string(Config::DebugOverlay ? "ON" : "OFF"),
        "Allow Quadbikes: " + std::string(Config::AllowQuadbikes ? "ON" : "OFF"),
        "Use Real Clutch: " + std::string(Config::UseRealClutch ? "ON" : "OFF")
    };

    for (int i = 0; i < numItems; ++i) {
        if (i == selectedIndex) {
            DrawRect(menuX, currentY, menuWidth, itemHeight, 255, 255, 255, 200); // Highlight
            DrawTextStr(itemNames[i], menuX + 0.005f, currentY + 0.005f, 0.35f, 0, 0, 0, 255);
        } else {
            DrawRect(menuX, currentY, menuWidth, itemHeight, 20, 20, 20, 200); // Normal
            DrawTextStr(itemNames[i], menuX + 0.005f, currentY + 0.005f, 0.35f, 255, 255, 255, 255);
        }
        currentY += itemHeight;
    }
}
