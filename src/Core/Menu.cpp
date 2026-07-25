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

bool Menu::waitingForKeyBind = false;

std::string Menu::VkName(int vk) {
  if (vk >= 'A' && vk <= 'Z')
    return std::string(1, static_cast<char>(vk));
  if (vk >= '0' && vk <= '9')
    return std::string(1, static_cast<char>(vk));
  if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
    return "NUMPAD " + std::to_string(vk - VK_NUMPAD0);
  switch (vk) {
  case VK_LBUTTON:
    return "LMB";
  case VK_RBUTTON:
    return "RMB";
  case VK_MBUTTON:
    return "MMB";
  case VK_XBUTTON1:
    return "MOUSE4";
  case VK_XBUTTON2:
    return "MOUSE5";
  case VK_SPACE:
    return "SPACE";
  case VK_RETURN:
    return "ENTER";
  case VK_SHIFT:
    return "SHIFT";
  case VK_CONTROL:
    return "CTRL";
  case VK_MENU:
    return "ALT";
  case VK_TAB:
    return "TAB";
  case VK_ESCAPE:
    return "ESC";
  case VK_UP:
    return "UP";
  case VK_DOWN:
    return "DOWN";
  case VK_LEFT:
    return "LEFT";
  case VK_RIGHT:
    return "RIGHT";
  default:
    return "VK " + std::to_string(vk);
  }
}

void Menu::Initialize() {
  menus.clear();
  menuStack.clear();

  // 0: Main Menu
  Submenu main;
  main.title = "MANUAL TRANS";
  main.items.push_back(MenuItem("Main Settings", MenuItem::Submenu, 1));
  main.items.push_back(MenuItem("Controls / Keybinds", MenuItem::Submenu, 4));
  main.items.push_back(MenuItem("HUD Settings", MenuItem::Submenu, 3));
  main.items.push_back(MenuItem("Engine / Stall", MenuItem::Submenu, 5));
  main.items.push_back(MenuItem("Clutch", MenuItem::Submenu, 6));
  main.items.push_back(MenuItem("ABS / TCS", MenuItem::Submenu, 7));
  main.items.push_back(MenuItem("Gearbox Penalty", MenuItem::Submenu, 8));
  main.items.push_back(MenuItem("Automatic D / S", MenuItem::Submenu, 9));
  menus.push_back(main);

  // 1: Main Settings
  Submenu settings;
  settings.title = "MAIN SETTINGS";
  settings.items.push_back(MenuItem(
      "Transmission Mode", MenuItem::IntChoice, &Config::TransmissionMode,
      0, 2, {"OFF", "AUTOMATIC", "MANUAL"}));
  settings.items.push_back(MenuItem("Recalibrate Transmission", MenuItem::Bool,
                                    &Config::ForceRecalibrate));
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
  analog.items.push_back(MenuItem("Clutch Attack", MenuItem::Float,
                                  &Config::ClutchAttack, 0.01f, 0.01f, 1.0f));
  analog.items.push_back(MenuItem("Clutch Release", MenuItem::Float,
                                  &Config::ClutchRelease, 0.01f, 0.01f, 1.0f));
  menus.push_back(analog);

  // 3: HUD Settings
  Submenu hud;
  hud.title = "HUD SETTINGS";
  hud.items.push_back(
      MenuItem("Verbose Logging", MenuItem::Bool, &Config::DebugOverlay));
  hud.items.push_back(
      MenuItem("Overlay Pedal Bars", MenuItem::Bool, &Config::OverlayBars));
  hud.items.push_back(MenuItem("Overlay X", MenuItem::Float,
                               &Config::OverlayPosX,
                               0.01f, 0.00f, 0.85f));
  hud.items.push_back(MenuItem("Overlay Y", MenuItem::Float,
                               &Config::OverlayPosY,
                               0.01f, 0.05f, 0.85f));
  hud.items.push_back(MenuItem("Bar Width", MenuItem::Float,
                               &Config::OverlayBarWidth,
                               0.01f, 0.06f, 0.20f));
  hud.items.push_back(MenuItem("Bar Height", MenuItem::Float,
                               &Config::OverlayBarHeight,
                               0.002f, 0.008f, 0.030f));
  menus.push_back(hud);

  // 4: Controls / Keybinds
  Submenu keys;
  keys.title = "CONTROLS & BINDS";
  keys.items.push_back(
      MenuItem("Shift Up", MenuItem::KeyBind, &Config::KeyShiftUp));
  keys.items.push_back(
      MenuItem("Shift Down", MenuItem::KeyBind, &Config::KeyShiftDown));
  keys.items.push_back(
      MenuItem("Clutch", MenuItem::KeyBind, &Config::KeyClutch));
  keys.items.push_back(
      MenuItem("Engine On/Off", MenuItem::KeyBind, &Config::KeyEngine));
  keys.items.push_back(MenuItem("Menu", MenuItem::KeyBind, &Config::KeyMenu));
  keys.items.push_back(
      MenuItem("Turn Signal Left", MenuItem::KeyBind, &Config::KeySignalLeft));
  keys.items.push_back(MenuItem("Turn Signal Right", MenuItem::KeyBind,
                                &Config::KeySignalRight));
  keys.items.push_back(MenuItem("Parking Brake", MenuItem::KeyBind,
                                &Config::KeyParkingBrake));
  menus.push_back(keys);

  Submenu engine;
  engine.title = "ENGINE / STALL";
  engine.items.push_back(
      MenuItem("Idle Creep", MenuItem::Bool, &Config::IdleCreep));
  engine.items.push_back(MenuItem("Creep Throttle", MenuItem::Float,
                                  &Config::IdleCreepThrottle,
                                  0.01f, 0.00f, 0.40f));
  engine.items.push_back(
      MenuItem("Engine Stall", MenuItem::Bool, &Config::StallEnabled));
  engine.items.push_back(MenuItem("Stall Rate", MenuItem::Float,
                                  &Config::StallRate,
                                  0.05f, 0.10f, 4.00f));
  engine.items.push_back(MenuItem("Stall Clutch", MenuItem::Float,
                                  &Config::StallClutchThreshold,
                                  0.01f, 0.30f, 0.95f));
  engine.items.push_back(MenuItem("Idle Torque", MenuItem::Float,
                                  &Config::IdleTorqueFraction,
                                  0.01f, 0.02f, 0.60f));
  engine.items.push_back(MenuItem("Lug Stall RPM", MenuItem::Float,
                                  &Config::LugStallRPM,
                                  50.0f, 800.0f, 2500.0f));
  engine.items.push_back(MenuItem("Lug Stall Delay", MenuItem::Float,
                                  &Config::LugStallDelay,
                                  0.10f, 0.40f, 8.00f));
  engine.items.push_back(MenuItem("Water Stall Delay", MenuItem::Float,
                                  &Config::WaterStallDelay,
                                  0.10f, 0.50f, 12.00f));
  engine.items.push_back(MenuItem("Rollover Stall", MenuItem::Float,
                                  &Config::RolloverStallDelay,
                                  0.25f, 1.00f, 20.00f));
  engine.items.push_back(MenuItem("Rev Hang", MenuItem::Float,
                                  &Config::RevHangDuration,
                                  0.05f, 0.00f, 2.00f));
  engine.items.push_back(MenuItem("Hard Brake Stall", MenuItem::Bool,
                                  &Config::HardBrakeStall));
  engine.items.push_back(MenuItem("Fuel Cut Engine Brake", MenuItem::Bool,
                                  &Config::FuelCutoffEngineBrake));
  engine.items.push_back(MenuItem(
      "Starter Interlock", MenuItem::Bool, &Config::StarterInterlock));
  engine.items.push_back(MenuItem(
      "Auto Start Needs Brake", MenuItem::Bool,
      &Config::AutomaticStartRequiresBrake));
  engine.items.push_back(
      MenuItem("Launch Control", MenuItem::Bool, &Config::LaunchControl));
  engine.items.push_back(MenuItem("Launch RPM", MenuItem::Float,
                                  &Config::LaunchControlRPM,
                                  0.01f, 0.40f, 0.95f));
  menus.push_back(engine);

  Submenu clutchMenu;
  clutchMenu.title = "CLUTCH";
  clutchMenu.items.push_back(MenuItem("Pedal Attack", MenuItem::Float,
                                      &Config::ClutchAttack,
                                      0.005f, 0.005f, 0.50f));
  clutchMenu.items.push_back(MenuItem("Pedal Release", MenuItem::Float,
                                      &Config::ClutchRelease,
                                      0.005f, 0.005f, 0.50f));
  clutchMenu.items.push_back(MenuItem("Pedal Expo", MenuItem::Float,
                                      &Config::ClutchExpo,
                                      0.01f, 0.00f, 1.00f));
  clutchMenu.items.push_back(MenuItem("Bite Start", MenuItem::Float,
                                      &Config::ClutchBiteStart,
                                      0.01f, 0.02f, 0.80f));
  clutchMenu.items.push_back(MenuItem("Bite End", MenuItem::Float,
                                      &Config::ClutchBiteEnd,
                                      0.01f, 0.10f, 0.98f));
  clutchMenu.items.push_back(MenuItem("Heat Rate", MenuItem::Float,
                                      &Config::ClutchHeatRate,
                                      0.01f, 0.00f, 0.50f));
  clutchMenu.items.push_back(MenuItem("Cool Rate", MenuItem::Float,
                                      &Config::ClutchCoolRate,
                                      0.005f, 0.00f, 0.30f));
  clutchMenu.items.push_back(MenuItem("Fade Start", MenuItem::Float,
                                      &Config::ClutchFadeStart,
                                      0.01f, 0.50f, 0.99f));
  clutchMenu.items.push_back(MenuItem("Fade Strength", MenuItem::Float,
                                      &Config::ClutchFadeStrength,
                                      0.01f, 0.00f, 1.00f));
  clutchMenu.items.push_back(MenuItem("Max Clutch Torque", MenuItem::Float,
                                      &Config::MaxClutchTorque,
                                      0.05f, 0.30f, 2.00f));
  clutchMenu.items.push_back(MenuItem("Hot Clutch Judder", MenuItem::Bool,
                                      &Config::ClutchJudder));
  clutchMenu.items.push_back(MenuItem("Dump Rate", MenuItem::Float,
                                      &Config::ClutchDumpRate,
                                      0.25f, 1.00f, 30.00f));
  clutchMenu.items.push_back(MenuItem("Dump Shock", MenuItem::Float,
                                      &Config::ClutchDumpShock,
                                      0.01f, 0.00f, 1.00f));
  menus.push_back(clutchMenu);

  Submenu assists;
  assists.title = "ABS / TCS";
  assists.items.push_back(
      MenuItem("TCS", MenuItem::Bool, &Config::TcsEnabled));
  assists.items.push_back(MenuItem("TCS Slip Target", MenuItem::Float,
                                   &Config::TcsSlipTarget,
                                   0.01f, 0.02f, 0.60f));
  assists.items.push_back(MenuItem("TCS Max Cut", MenuItem::Float,
                                   &Config::TcsMaxCut,
                                   0.01f, 0.00f, 1.00f));
  assists.items.push_back(
      MenuItem("ABS", MenuItem::Bool, &Config::AbsEnabled));
  assists.items.push_back(MenuItem("ABS Slip Target", MenuItem::Float,
                                   &Config::AbsSlipTarget,
                                   0.01f, 0.05f, 0.60f));
  assists.items.push_back(MenuItem("ABS Max Release", MenuItem::Float,
                                   &Config::AbsMaxRelease,
                                   0.01f, 0.00f, 1.00f));
  assists.items.push_back(MenuItem(
      "Brake Fade", MenuItem::Bool, &Config::BrakeFadeEnabled));
  assists.items.push_back(MenuItem("Brake Heat Rate", MenuItem::Float,
                                   &Config::BrakeHeatRate,
                                   0.001f, 0.000f, 0.100f));
  assists.items.push_back(MenuItem("Brake Cool Rate", MenuItem::Float,
                                   &Config::BrakeCoolRate,
                                   0.001f, 0.000f, 0.150f));
  assists.items.push_back(MenuItem("Brake Fade Start", MenuItem::Float,
                                   &Config::BrakeFadeStart,
                                   0.01f, 0.40f, 0.99f));
  assists.items.push_back(MenuItem("Brake Fade Strength", MenuItem::Float,
                                   &Config::BrakeFadeStrength,
                                   0.01f, 0.00f, 0.90f));
  menus.push_back(assists);

  Submenu gearbox;
  gearbox.title = "GEARBOX PENALTY";
  gearbox.items.push_back(
      MenuItem("Gear Clash", MenuItem::Bool, &Config::GearClash));
  gearbox.items.push_back(MenuItem("Grind Damage", MenuItem::Float,
                                   &Config::GearGrindDamage,
                                   0.005f, 0.00f, 0.20f));
  gearbox.items.push_back(MenuItem("Shift Shock", MenuItem::Float,
                                   &Config::ShiftShockStrength,
                                   0.01f, 0.00f, 1.00f));
  gearbox.items.push_back(MenuItem("No-lift Penalty", MenuItem::Float,
                                   &Config::NoLiftShiftPenalty,
                                   0.01f, 0.00f, 1.00f));
  gearbox.items.push_back(MenuItem("Synchronizer Wear", MenuItem::Bool,
                                   &Config::SynchronizerWear));
  gearbox.items.push_back(MenuItem("Shift Resistance", MenuItem::Bool,
                                   &Config::ShiftResistance));
  gearbox.items.push_back(MenuItem("Native Gearbox Override", MenuItem::Bool,
                                   &Config::NativeGearboxPatch));
  gearbox.items.push_back(MenuItem("Reverse Lockout km/h", MenuItem::Float,
                                   &Config::ReverseLockoutSpeedKmH,
                                   0.50f, 0.00f, 30.00f));
  gearbox.items.push_back(MenuItem("Over-rev Damage", MenuItem::Float,
                                   &Config::OverRevShiftDamage,
                                   0.01f, 0.00f, 0.50f));
  menus.push_back(gearbox);

  Submenu automatic;
  automatic.title = "AUTOMATIC D/S/L";
  automatic.items.push_back(MenuItem(
      "Brake Interlock", MenuItem::Bool, &Config::AutomaticBrakeInterlock));
  automatic.items.push_back(MenuItem(
      "Shift Delay", MenuItem::Float, &Config::AutomaticShiftDelay,
      0.01f, 0.10f, 1.20f));
  automatic.items.push_back(MenuItem(
      "D Upshift RPM", MenuItem::Float, &Config::AutomaticDUpRPM,
      0.01f, 0.35f, 0.80f));
  automatic.items.push_back(MenuItem(
      "D Downshift RPM", MenuItem::Float, &Config::AutomaticDDownRPM,
      0.01f, 0.10f, 0.70f));
  automatic.items.push_back(MenuItem(
      "S Upshift RPM", MenuItem::Float, &Config::AutomaticSUpRPM,
      0.01f, 0.50f, 0.99f));
  automatic.items.push_back(MenuItem(
      "S Downshift RPM", MenuItem::Float, &Config::AutomaticSDownRPM,
      0.01f, 0.15f, 0.80f));
  automatic.items.push_back(MenuItem(
      "Kickdown Pedal", MenuItem::Float, &Config::AutomaticKickdownThrottle,
      0.01f, 0.40f, 0.98f));
  automatic.items.push_back(MenuItem(
      "S Pedal Response", MenuItem::Float, &Config::AutomaticSTorqueBoost,
      0.01f, 0.00f, 0.50f));
  automatic.items.push_back(MenuItem(
      "D Keyboard Ceiling", MenuItem::Float,
      &Config::AutomaticDKeyboardThrottle,
      0.01f, 0.30f, 1.00f));
  automatic.items.push_back(MenuItem(
      "Kickdown Delay", MenuItem::Float, &Config::AutomaticKickdownDelay,
      0.05f, 0.20f, 1.50f));
  automatic.items.push_back(MenuItem(
      "Torque Converter Lock", MenuItem::Bool, &Config::AutomaticTCC));
  automatic.items.push_back(MenuItem(
      "Fluid Overheat / Limp", MenuItem::Bool,
      &Config::AutomaticFluidOverheat));
  automatic.items.push_back(MenuItem(
      "Neutral Drop Damage", MenuItem::Bool,
      &Config::AutomaticNeutralDropDamage));
  automatic.items.push_back(MenuItem(
      "Brake Boost Stall", MenuItem::Bool,
      &Config::AutomaticBrakeBoostStall));
  automatic.items.push_back(MenuItem(
      "Throttle Attack", MenuItem::Float, &Config::AutomaticThrottleAttack,
      0.01f, 0.01f, 1.50f));
  automatic.items.push_back(MenuItem(
      "Throttle Release", MenuItem::Float, &Config::AutomaticThrottleRelease,
      0.01f, 0.01f, 1.50f));
  automatic.items.push_back(MenuItem(
      "Brake Attack", MenuItem::Float, &Config::AutomaticBrakeAttack,
      0.01f, 0.01f, 1.50f));
  automatic.items.push_back(MenuItem(
      "Brake Release", MenuItem::Float, &Config::AutomaticBrakeRelease,
      0.01f, 0.01f, 1.50f));
  automatic.items.push_back(MenuItem(
      "Brake Overrides Gas", MenuItem::Bool,
      &Config::BrakeThrottleOverride));
  automatic.items.push_back(MenuItem(
      "Override Delay", MenuItem::Float, &Config::BrakeOverrideDelay,
      0.01f, 0.00f, 1.00f));
  automatic.items.push_back(MenuItem(
      "Override Cut", MenuItem::Float, &Config::BrakeOverrideCut,
      0.01f, 0.00f, 1.00f));
  menus.push_back(automatic);

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

  if (!isOpen || menus.empty())
    return;

  // Disable game's UI and weapon controls to prevent overlap
  const int controlsToDisable[] = {
      19,  // INPUT_CHARACTER_WHEEL
      27,  // INPUT_PHONE
      37,  // INPUT_SELECT_WEAPON
      85,  // INPUT_VEH_RADIO_WHEEL
      140, // INPUT_MELEE_ATTACK_LIGHT
      172, // INPUT_CELLPHONE_UP
      173, // INPUT_CELLPHONE_DOWN
      174, // INPUT_CELLPHONE_LEFT
      175, // INPUT_CELLPHONE_RIGHT
      176, // INPUT_CELLPHONE_SELECT
      177, // INPUT_CELLPHONE_CANCEL
      261, // INPUT_PREV_WEAPON
      262  // INPUT_NEXT_WEAPON
  };
  for (int c : controlsToDisable) {
    PAD::DISABLE_CONTROL_ACTION(0, c, TRUE);
  }

  Submenu &current = GetCurrentMenu();
  MenuItem &selectedItem = current.items[current.selectedIndex];

  if (waitingForKeyBind && selectedItem.type == MenuItem::KeyBind &&
      selectedItem.keyVal) {
    if (PAD::IS_CONTROL_JUST_PRESSED(0, 177) ||
        (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
      // Cancel bind
      waitingForKeyBind = false;
      return;
    }
    // Check all possible keys
    for (int k = 1; k < 256; ++k) {
      if ((GetAsyncKeyState(k) & 0x8000) != 0 && k != VK_ESCAPE &&
          k != VK_RETURN && k != Config::KeyMenu) {
        *selectedItem.keyVal = k;
        Config::SaveConfig(g_pluginModule);

        // Wait until the user releases the key so it doesn't immediately
        // trigger another menu action on the very next frame.
        while ((GetAsyncKeyState(k) & 0x8000) != 0) {
          scriptWait(0);
        }

        waitingForKeyBind = false;
        return;
      }
    }
    return; // Block other inputs while waiting
  }

  if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 172)) { // UP
    current.selectedIndex--;
    if (current.selectedIndex < 0)
      current.selectedIndex = static_cast<int>(current.items.size()) - 1;
  }
  if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 173)) { // DOWN
    current.selectedIndex++;
    if (current.selectedIndex >= static_cast<int>(current.items.size()))
      current.selectedIndex = 0;
  }

  if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 177)) { // BACKSPACE / B
    if (menuStack.size() > 1) {
      menuStack.pop_back();
      Config::SaveConfig(g_pluginModule); // Save on back
    } else {
      Toggle(); // Close menu
      Config::SaveConfig(g_pluginModule);
    }
  }

  if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 176)) { // ENTER / A
    if (selectedItem.type == MenuItem::Bool && selectedItem.boolVal) {
      *selectedItem.boolVal = !(*selectedItem.boolVal);
      Config::SaveConfig(g_pluginModule);
    } else if (selectedItem.type == MenuItem::Submenu) {
      menuStack.push_back(selectedItem.targetSubmenu);
    } else if (selectedItem.type == MenuItem::KeyBind) {
      waitingForKeyBind = true;
    }
  }

  if (selectedItem.type == MenuItem::Float && selectedItem.floatVal) {
    if (PAD::IS_DISABLED_CONTROL_PRESSED(0, 174) ||
        PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 174)) { // LEFT
      *selectedItem.floatVal -= selectedItem.floatStep;
      if (*selectedItem.floatVal < selectedItem.floatMin)
        *selectedItem.floatVal = selectedItem.floatMin;
    }
    if (PAD::IS_DISABLED_CONTROL_PRESSED(0, 175) ||
        PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 175)) { // RIGHT
      *selectedItem.floatVal += selectedItem.floatStep;
      if (*selectedItem.floatVal > selectedItem.floatMax)
        *selectedItem.floatVal = selectedItem.floatMax;
    }
  }

  if (selectedItem.type == MenuItem::IntChoice && selectedItem.keyVal) {
    if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 174)) {
      *selectedItem.keyVal =
          (std::max)(selectedItem.intMin, *selectedItem.keyVal - 1);
      Config::SaveConfig(g_pluginModule);
    }
    if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 175)) {
      *selectedItem.keyVal =
          (std::min)(selectedItem.intMax, *selectedItem.keyVal + 1);
      Config::SaveConfig(g_pluginModule);
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
    } else if (item.type == MenuItem::IntChoice && item.keyVal) {
      const int index = *item.keyVal - item.intMin;
      if (index >= 0 &&
          index < static_cast<int>(item.choiceLabels.size())) {
        sprintf_s(valBuf, "< %s >", item.choiceLabels[index].c_str());
      } else {
        sprintf_s(valBuf, "< %d >", *item.keyVal);
      }
    } else if (item.type == MenuItem::Submenu) {
      sprintf_s(valBuf, ">>>");
    } else if (item.type == MenuItem::KeyBind && item.keyVal) {
      if (waitingForKeyBind && isSelected) {
        sprintf_s(valBuf, "[PRESS KEY]");
      } else {
        sprintf_s(valBuf, "[%s]", VkName(*item.keyVal).c_str());
      }
    }

    if (valBuf[0] != '\0') {
      DrawTextStr(valBuf, menuX + menuWidth - 0.05f, currentY + 0.005f, 0.35f,
                  textR, textG, textB, 255, true);
    }

    currentY += itemHeight;
  }
}
