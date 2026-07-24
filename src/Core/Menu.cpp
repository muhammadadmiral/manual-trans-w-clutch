#include "Menu.h"
#include "../../sdk/inc/natives.h"
#include "Config.h"
#include <algorithm>
#include <cstdio>
#include <vector>

bool Menu::isOpen = false;
std::vector<Menu::Submenu> Menu::menus;
std::vector<int> Menu::menuStack;

extern HMODULE g_pluginModule; // Needed to save INI, from main.cpp

void Menu::Initialize() {
  menus.clear();
  menuStack.clear();

  // 0: Main Menu
  Submenu main;
  main.title = "MANUAL TRANS";
  main.items.push_back(MenuItem("Main Settings", MenuItem::Submenu, 1));
  main.items.push_back(MenuItem("Analog Tuning", MenuItem::Submenu, 2));
  main.items.push_back(MenuItem("HUD Settings", MenuItem::Submenu, 3));
  menus.push_back(main);

  // 1: Main Settings
  Submenu settings;
  settings.title = "MAIN SETTINGS";
  settings.items.push_back(MenuItem("Require Cold Start", MenuItem::Bool,
                                    &Config::RequireColdStart));
  settings.items.push_back(
      MenuItem("Allow Quadbikes", MenuItem::Bool, &Config::AllowQuadbikes));
  settings.items.push_back(
      MenuItem("Use Real Clutch", MenuItem::Bool, &Config::UseRealClutch));
  menus.push_back(settings);

  // 2: Analog Tuning
  Submenu analog;
  analog.title = "ANALOG TUNING";
  analog.items.push_back(MenuItem("Throttle Attack", MenuItem::Float,
                                  &Config::ThrottleAttack, 0.01f, 0.01f, 1.0f));
  analog.items.push_back(MenuItem("Throttle Release", MenuItem::Float,
                                  &Config::ThrottleRelease, 0.01f, 0.01f,
                                  1.0f));
  analog.items.push_back(MenuItem("Brake Attack", MenuItem::Float,
                                  &Config::BrakeAttack, 0.01f, 0.01f, 1.0f));
  analog.items.push_back(MenuItem("Brake Release", MenuItem::Float,
                                  &Config::BrakeRelease, 0.01f, 0.01f, 1.0f));
  analog.items.push_back(MenuItem("Clutch Attack", MenuItem::Float,
                                  &Config::ClutchAttack, 0.01f, 0.01f, 1.0f));
  analog.items.push_back(MenuItem("Clutch Release", MenuItem::Float,
                                  &Config::ClutchRelease, 0.01f, 0.01f, 1.0f));
  menus.push_back(analog);

  // 3: HUD Settings
  Submenu hud;
  hud.title = "HUD SETTINGS";
  hud.items.push_back(
      MenuItem("Overlay Status UI", MenuItem::Bool, &Config::DebugOverlay));
  hud.items.push_back(
      MenuItem("Overlay Pedal Bars", MenuItem::Bool, &Config::OverlayBars));
  menus.push_back(hud);

  menuStack.push_back(0); // Push Main Menu
}

void Menu::Toggle() {
  isOpen = !isOpen;
  if (isOpen && menus.empty()) {
    Initialize();
  }
}

bool Menu::IsOpen() { return isOpen; }

int Menu::GetCurrentMenuIndex() {
  if (menuStack.empty())
    return 0;
  return menuStack.back();
}

Menu::Submenu &Menu::GetCurrentMenu() { return menus[GetCurrentMenuIndex()]; }

void Menu::DrawRect(float x, float y, float width, float height, int r, int g,
                    int b, int a) {
  GRAPHICS::DRAW_RECT(x + width * 0.5f, y + height * 0.5f, width, height, r, g,
                      b, a, 0);
}

void Menu::DrawTextStr(const std::string &text, float x, float y, float scale,
                       int r, int g, int b, int a, bool center) {
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
    static bool wasMenuKeyPressed = false;
    bool isMenuKeyPressed = (GetAsyncKeyState(Config::KeyMenu) & 0x8000) != 0;

    if (isMenuKeyPressed && !wasMenuKeyPressed) {
        Toggle();
    }
    wasMenuKeyPressed = isMenuKeyPressed;

    if (!isOpen || menus.empty()) return;

  Submenu &current = GetCurrentMenu();

  if (PAD::IS_CONTROL_JUST_PRESSED(0, 172)) { // UP
    current.selectedIndex--;
    if (current.selectedIndex < 0)
      current.selectedIndex = static_cast<int>(current.items.size()) - 1;
  }
  if (PAD::IS_CONTROL_JUST_PRESSED(0, 173)) { // DOWN
    current.selectedIndex++;
    if (current.selectedIndex >= static_cast<int>(current.items.size()))
      current.selectedIndex = 0;
  }

  if (PAD::IS_CONTROL_JUST_PRESSED(0, 177)) { // BACKSPACE / B
    if (menuStack.size() > 1) {
      menuStack.pop_back();
      Config::SaveConfig(g_pluginModule); // Save on back
    } else {
      Toggle(); // Close menu
      Config::SaveConfig(g_pluginModule);
    }
  }

  MenuItem &selectedItem = current.items[current.selectedIndex];

  if (PAD::IS_CONTROL_JUST_PRESSED(0, 176)) { // ENTER / A
    if (selectedItem.type == MenuItem::Bool && selectedItem.boolVal) {
      *selectedItem.boolVal = !(*selectedItem.boolVal);
      Config::SaveConfig(g_pluginModule);
    } else if (selectedItem.type == MenuItem::Submenu) {
      menuStack.push_back(selectedItem.targetSubmenu);
    }
  }

  if (selectedItem.type == MenuItem::Float && selectedItem.floatVal) {
    if (PAD::IS_CONTROL_PRESSED(0, 174) ||
        PAD::IS_CONTROL_JUST_PRESSED(0, 174)) { // LEFT
      *selectedItem.floatVal -= selectedItem.floatStep;
      if (*selectedItem.floatVal < selectedItem.floatMin)
        *selectedItem.floatVal = selectedItem.floatMin;
    }
    if (PAD::IS_CONTROL_PRESSED(0, 175) ||
        PAD::IS_CONTROL_JUST_PRESSED(0, 175)) { // RIGHT
      *selectedItem.floatVal += selectedItem.floatStep;
      if (*selectedItem.floatVal > selectedItem.floatMax)
        *selectedItem.floatVal = selectedItem.floatMax;
    }
  }
}

void Menu::Draw() {
  if (!isOpen || menus.empty())
    return;

  Submenu &current = GetCurrentMenu();

  const float menuX = 0.75f;
  const float menuY = 0.2f;
  const float menuWidth = 0.2f;
  const float itemHeight = 0.035f;
  const float headerHeight = 0.08f;

  // Header
  DrawRect(menuX, menuY, menuWidth, headerHeight, 200, 50, 50, 255);
  DrawTextStr(current.title, menuX + menuWidth / 2, menuY + 0.02f, 0.6f, 255,
              255, 255, 255, true);

  float currentY = menuY + headerHeight;

  for (size_t i = 0; i < current.items.size(); ++i) {
    MenuItem &item = current.items[i];
    bool isSelected = (static_cast<int>(i) == current.selectedIndex);

    int bgR = isSelected ? 255 : 20;
    int bgG = isSelected ? 255 : 20;
    int bgB = isSelected ? 255 : 20;
    int textR = isSelected ? 0 : 255;
    int textG = isSelected ? 0 : 255;
    int textB = isSelected ? 0 : 255;

    DrawRect(menuX, currentY, menuWidth, itemHeight, bgR, bgG, bgB, 200);
    DrawTextStr(item.name, menuX + 0.005f, currentY + 0.005f, 0.35f, textR,
                textG, textB, 255);

    // Draw Value
    char valBuf[64]{};
    if (item.type == MenuItem::Bool && item.boolVal) {
      sprintf_s(valBuf, "%s", *item.boolVal ? "ON" : "OFF");
    } else if (item.type == MenuItem::Float && item.floatVal) {
      sprintf_s(valBuf, "< %.2f >", *item.floatVal);
    } else if (item.type == MenuItem::Submenu) {
      sprintf_s(valBuf, ">>>");
    }

    if (valBuf[0] != '\0') {
        DrawTextStr(valBuf, menuX + menuWidth - 0.05f, currentY + 0.005f, 0.35f, textR, textG, textB, 255, true);
    }

    currentY += itemHeight;
  }
}
