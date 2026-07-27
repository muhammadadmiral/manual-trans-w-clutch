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
  main.title = "MELAR TRANSMISSION";
  main.items.push_back(MenuItem("Main Settings", MenuItem::Submenu, 1));
  main.items.push_back(MenuItem("Controls / Keybinds", MenuItem::Submenu, 4));
  main.items.push_back(MenuItem("HUD Settings", MenuItem::Submenu, 3));
  main.items.push_back(MenuItem("Engine / Stall", MenuItem::Submenu, 5));
  main.items.push_back(MenuItem("Clutch", MenuItem::Submenu, 6));
  main.items.push_back(MenuItem("ABS / TCS", MenuItem::Submenu, 7));
  main.items.push_back(MenuItem("Gearbox Penalty", MenuItem::Submenu, 8));
  main.items.push_back(MenuItem("Automatic D / S", MenuItem::Submenu, 9));
  main.items.push_back(MenuItem("Audio", MenuItem::Submenu, 10));
  main.items.push_back(MenuItem("Fuel / Maintenance", MenuItem::Submenu, 11));
  main.items.push_back(MenuItem("Speedometer Studio", MenuItem::Submenu, 12));
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
  analog.title = "PEDAL CALIBRATION (LSC)";
  analog.items.push_back(MenuItem("Throttle Attack", MenuItem::Float,
                                  &Config::ThrottleAttack,
                                  0.01f, 0.01f, 1.50f));
  analog.items.push_back(MenuItem("Throttle Release", MenuItem::Float,
                                  &Config::ThrottleRelease,
                                  0.01f, 0.01f, 1.50f));
  analog.items.push_back(MenuItem("Throttle Curve", MenuItem::Float,
                                  &Config::ThrottleExpo,
                                  0.01f, 0.00f, 1.00f));
  analog.items.push_back(MenuItem("Brake Attack", MenuItem::Float,
                                  &Config::BrakeAttack,
                                  0.01f, 0.01f, 1.50f));
  analog.items.push_back(MenuItem("Brake Release", MenuItem::Float,
                                  &Config::BrakeRelease,
                                  0.01f, 0.01f, 1.50f));
  analog.items.push_back(MenuItem("Brake Curve", MenuItem::Float,
                                  &Config::BrakeExpo,
                                  0.01f, 0.00f, 1.00f));
  analog.items.push_back(MenuItem("Clutch Attack", MenuItem::Float,
                                  &Config::ClutchAttack, 0.01f, 0.01f, 1.0f));
  analog.items.push_back(MenuItem("Clutch Release", MenuItem::Float,
                                  &Config::ClutchRelease, 0.01f, 0.01f, 1.0f));
  analog.items.push_back(MenuItem("Clutch Curve", MenuItem::Float,
                                  &Config::ClutchExpo,
                                  0.01f, 0.00f, 1.00f));
  menus.push_back(analog);

  // 3: HUD Settings
  Submenu hud;
  hud.title = "HUD SETTINGS";
  hud.items.push_back(
      MenuItem("Verbose Logging", MenuItem::Bool, &Config::DebugOverlay));
  hud.items.push_back(
      MenuItem("Overlay Pedal Bars", MenuItem::Bool, &Config::OverlayBars));
  hud.items.push_back(
      MenuItem("Gear HUD", MenuItem::Bool, &Config::GearHudEnabled));
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
  hud.items.push_back(MenuItem("Gear HUD X", MenuItem::Float,
                               &Config::GearHudPosX,
                               0.005f, 0.05f, 0.95f));
  hud.items.push_back(MenuItem("Gear HUD Y", MenuItem::Float,
                               &Config::GearHudPosY,
                               0.005f, 0.08f, 0.90f));
  hud.items.push_back(MenuItem("Gear HUD Scale", MenuItem::Float,
                               &Config::GearHudScale,
                               0.05f, 0.65f, 1.50f));
  hud.items.push_back(MenuItem("Menu X", MenuItem::Float,
                               &Config::MenuPosX,
                               0.005f, 0.00f, 0.78f));
  hud.items.push_back(MenuItem("Menu Y", MenuItem::Float,
                               &Config::MenuPosY,
                               0.005f, 0.02f, 0.55f));
  hud.items.push_back(MenuItem("Menu Scale", MenuItem::Float,
                               &Config::MenuScale,
                               0.05f, 0.70f, 1.20f));
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
  keys.items.push_back(MenuItem("Hazard Lights", MenuItem::KeyBind,
                                &Config::KeySignalHazard));
  keys.items.push_back(MenuItem("Parking Brake", MenuItem::KeyBind,
                                &Config::KeyParkingBrake));
  keys.items.push_back(
      MenuItem("Refuel (hold)", MenuItem::KeyBind, &Config::KeyRefuel));
  keys.items.push_back(
      MenuItem("Oil Service (hold)", MenuItem::KeyBind,
               &Config::KeyOilService));
  keys.items.push_back(
      MenuItem("LSC Workshop", MenuItem::KeyBind, &Config::KeyWorkshop));
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
  engine.items.push_back(MenuItem("Actual Stall Cutoff", MenuItem::Float,
                                  &Config::StallCutoffRPM,
                                  25.0f, 450.0f, 1800.0f));
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
  assists.title = "DRIVE ASSISTS";
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
  assists.items.push_back(
      MenuItem("ESC", MenuItem::Bool, &Config::EscEnabled));
  assists.items.push_back(MenuItem("ESC Min Speed", MenuItem::Float,
                                   &Config::EscMinSpeedKmH,
                                   1.0f, 5.0f, 80.0f));
  assists.items.push_back(MenuItem("ESC Slip Angle", MenuItem::Float,
                                   &Config::EscSlipAngleThresholdDeg,
                                   0.5f, 2.0f, 20.0f));
  assists.items.push_back(MenuItem("ESC Throttle Cut", MenuItem::Float,
                                   &Config::EscMaxThrottleCut,
                                   0.01f, 0.0f, 1.0f));
  assists.items.push_back(MenuItem("ESC Brake Force", MenuItem::Float,
                                   &Config::EscBrakeStrength,
                                   0.01f, 0.0f, 0.8f));
  assists.items.push_back(MenuItem(
      "Rollover Assist", MenuItem::Bool, &Config::RolloverAssist));
  assists.items.push_back(MenuItem("Rollover Angle", MenuItem::Float,
                                   &Config::RolloverWarningAngleDeg,
                                   1.0f, 15.0f, 75.0f));
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

  Submenu audio;
  audio.title = "AUDIO";
  audio.items.push_back(
      MenuItem("Mechanical Audio", MenuItem::Bool, &Config::AudioEnabled));
  audio.items.push_back(MenuItem(
      "Master Volume", MenuItem::Float, &Config::AudioMasterVolume,
      0.02f, 0.00f, 1.00f));
  audio.items.push_back(MenuItem(
      "Random Pitch", MenuItem::Float, &Config::AudioPitchRandomness,
      0.005f, 0.00f, 0.18f));
  audio.items.push_back(MenuItem(
      "Limiter Ceiling", MenuItem::Float, &Config::AudioLimiterCeiling,
      0.02f, 0.25f, 0.95f));
  audio.items.push_back(MenuItem(
      "Native GTA Layers", MenuItem::Bool, &Config::AudioNativeLayers));
  audio.items.push_back(MenuItem(
      "Turbo / Blow-off", MenuItem::Bool, &Config::AudioTurboSounds));
  audio.items.push_back(MenuItem(
      "Clutch Slip", MenuItem::Bool, &Config::AudioClutchSounds));
  audio.items.push_back(MenuItem(
      "Gearbox / Clunk", MenuItem::Bool,
      &Config::AudioTransmissionSounds));
  audio.items.push_back(MenuItem(
      "Engine Load / Lug", MenuItem::Bool,
      &Config::AudioEngineLoadSounds));
  audio.items.push_back(MenuItem(
      "TCS / ABS / Launch", MenuItem::Bool,
      &Config::AudioAssistSounds));
  menus.push_back(audio);

  Submenu maintenance;
  maintenance.title = "FUEL / MAINTENANCE";
  maintenance.items.push_back(
      MenuItem("Fuel Simulation", MenuItem::Bool, &Config::FuelEnabled));
  maintenance.items.push_back(
      MenuItem("Fuel Station Blips", MenuItem::Bool,
               &Config::FuelBlipsEnabled));
  maintenance.items.push_back(MenuItem(
      "Refuel Speed", MenuItem::Float, &Config::RefuelRatePerSecond,
      0.005f, 0.005f, 0.25f));
  maintenance.items.push_back(
      MenuItem("Oil Maintenance", MenuItem::Bool,
               &Config::MaintenanceEnabled));
  maintenance.items.push_back(MenuItem(
      "Oil Wear", MenuItem::Float, &Config::OilWearMultiplier,
      0.10f, 0.00f, 5.00f));
  maintenance.items.push_back(
      MenuItem("LS Customs Service Bays", MenuItem::Bool,
               &Config::WorkshopEnabled));
  maintenance.items.push_back(MenuItem(
      "Workshop Radius", MenuItem::Float, &Config::WorkshopRadius,
      1.0f, 6.0f, 30.0f));
  menus.push_back(maintenance);

  Submenu speedometer;
  speedometer.title = "SPEEDOMETER STUDIO";
  speedometer.items.push_back(
      MenuItem("Custom Speedometer", MenuItem::Bool,
               &Config::SpeedometerEnabled));
  speedometer.items.push_back(MenuItem(
      "Car Layout", MenuItem::IntChoice, &Config::SpeedometerCarStyle,
      0, 9, {"GT DIGITAL", "TWIN ANALOG", "SPORT HYBRID",
             "RETRO TOURING", "MINIMAL DIGITAL", "ARC DIGITAL",
             "RALLY STACK", "LUXURY CLUSTER", "TRACK BAR",
             "AUTO DYNAMIC"}));
  speedometer.items.push_back(MenuItem(
      "Bike Layout", MenuItem::IntChoice, &Config::SpeedometerBikeStyle,
      0, 9, {"ROAD TFT", "ROUND CLASSIC", "SUPERSPORT TFT",
             "CAFE RACER", "MINIMAL ENDURO", "ARC TFT",
             "MOTOCROSS STACK", "CRUISER TWIN", "TRACK BAR",
             "AUTO DYNAMIC"}));
  speedometer.items.push_back(MenuItem(
      "Units", MenuItem::IntChoice, &Config::SpeedometerUnits,
      0, 1, {"KM/H", "MPH"}));
  speedometer.items.push_back(MenuItem(
      "Accent", MenuItem::IntChoice, &Config::SpeedometerAccent,
      0, 4, {"CYAN", "RED", "GREEN", "AMBER", "WHITE"}));
  speedometer.items.push_back(
      MenuItem("Status Icons", MenuItem::Bool,
               &Config::SpeedometerShowIcons));
  speedometer.items.push_back(
      MenuItem("Detailed Telemetry", MenuItem::Bool,
               &Config::SpeedometerDetailed));
  speedometer.items.push_back(MenuItem(
      "Position X", MenuItem::Float, &Config::SpeedometerPosX,
      0.005f, 0.12f, 0.92f));
  speedometer.items.push_back(MenuItem(
      "Position Y", MenuItem::Float, &Config::SpeedometerPosY,
      0.005f, 0.20f, 0.92f));
  speedometer.items.push_back(MenuItem(
      "Scale", MenuItem::Float, &Config::SpeedometerScale,
      0.05f, 0.65f, 1.40f));
  speedometer.items.push_back(MenuItem(
      "Opacity", MenuItem::Float, &Config::SpeedometerOpacity,
      0.05f, 0.35f, 1.00f));
  menus.push_back(speedometer);

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
    } else if (selectedItem.type == MenuItem::Action) {
      if (selectedItem.actionId == 1) {
        Config::ResetPedalsToDefault();
        Config::SaveConfig(g_pluginModule);
        HUD::BEGIN_TEXT_COMMAND_THEFEED_POST("STRING");
        HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(
            "~b~Pedal map~w~ reset ke default");
        HUD::END_TEXT_COMMAND_THEFEED_POST_TICKER(false, false);
      }
    }
  }

  if (selectedItem.type == MenuItem::Float && selectedItem.floatVal) {
    bool adjusted = false;
    if (PAD::IS_DISABLED_CONTROL_PRESSED(0, 174) ||
        PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 174)) { // LEFT
      *selectedItem.floatVal -= selectedItem.floatStep;
      if (*selectedItem.floatVal < selectedItem.floatMin)
        *selectedItem.floatVal = selectedItem.floatMin;
      adjusted = true;
    }
    if (PAD::IS_DISABLED_CONTROL_PRESSED(0, 175) ||
        PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 175)) { // RIGHT
      *selectedItem.floatVal += selectedItem.floatStep;
      if (*selectedItem.floatVal > selectedItem.floatMax)
        *selectedItem.floatVal = selectedItem.floatMax;
      adjusted = true;
    }
    if (adjusted &&
        (selectedItem.floatVal == &Config::ThrottleAttack ||
         selectedItem.floatVal == &Config::ThrottleRelease ||
         selectedItem.floatVal == &Config::ThrottleExpo ||
         selectedItem.floatVal == &Config::BrakeAttack ||
         selectedItem.floatVal == &Config::BrakeRelease ||
         selectedItem.floatVal == &Config::BrakeExpo ||
         selectedItem.floatVal == &Config::ClutchAttack ||
         selectedItem.floatVal == &Config::ClutchRelease ||
         selectedItem.floatVal == &Config::ClutchExpo)) {
      Config::PedalPreset = 4;
    }
  }

  if (selectedItem.type == MenuItem::IntChoice && selectedItem.keyVal) {
    if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 174)) {
      *selectedItem.keyVal =
          (std::max)(selectedItem.intMin, *selectedItem.keyVal - 1);
      if (selectedItem.keyVal == &Config::PedalPreset)
        Config::ApplyPedalPreset(*selectedItem.keyVal);
      Config::SaveConfig(g_pluginModule);
    }
    if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 175)) {
      *selectedItem.keyVal =
          (std::min)(selectedItem.intMax, *selectedItem.keyVal + 1);
      if (selectedItem.keyVal == &Config::PedalPreset)
        Config::ApplyPedalPreset(*selectedItem.keyVal);
      Config::SaveConfig(g_pluginModule);
    }
  }
}

void Menu::Draw() {
  if (!isOpen || menus.empty())
    return;

  Submenu &current = GetCurrentMenu();

  constexpr int maxVisibleItems = 12;
  const int itemCount = static_cast<int>(current.items.size());
  const int visibleCount = (std::min)(itemCount, maxVisibleItems);
  const int firstVisible =
      std::clamp(current.selectedIndex - maxVisibleItems / 2, 0,
                 (std::max)(0, itemCount - maxVisibleItems));

  const float menuScale = Config::MenuScale;
  const float menuWidth = 0.275f * menuScale;
  const float itemHeight = 0.036f * menuScale;
  const float headerHeight = 0.074f * menuScale;
  const float footerHeight = 0.038f * menuScale;
  const float panelHeight =
      headerHeight + itemHeight * visibleCount + footerHeight;
  const float safeSize =
      std::clamp(GRAPHICS::GET_SAFE_ZONE_SIZE(), 0.80f, 1.0f);
  const float safeInset = (1.0f - safeSize) * 0.50f + 0.012f;
  const float menuX =
      std::clamp(Config::MenuPosX, safeInset,
                 1.0f - safeInset - menuWidth);
  const float menuY =
      std::clamp(Config::MenuPosY, safeInset,
                 1.0f - safeInset - panelHeight);

  DrawRect(menuX - 0.004f * menuScale, menuY - 0.004f * menuScale,
           menuWidth + 0.008f * menuScale,
           panelHeight + 0.008f * menuScale,
           0, 0, 0, 145);
  DrawRect(menuX, menuY, menuWidth, panelHeight, 9, 13, 19, 238);
  DrawRect(menuX, menuY, menuWidth, headerHeight, 13, 24, 36, 255);
  DrawRect(menuX, menuY + headerHeight - 0.004f * menuScale,
           menuWidth, 0.004f * menuScale, 55, 205, 255, 255);
  DrawTextStr(current.title, menuX + 0.014f * menuScale,
              menuY + 0.018f * menuScale,
              0.52f * menuScale, 238, 246, 255, 255);
  DrawTextStr("DRIVETRAIN CONTROL",
              menuX + menuWidth - 0.075f * menuScale,
              menuY + 0.024f * menuScale,
              0.25f * menuScale, 95, 175, 205, 230, true);

  float currentY = menuY + headerHeight;

  for (int row = 0; row < visibleCount; ++row) {
    const int i = firstVisible + row;
    MenuItem &item = current.items[i];
    bool isSelected = i == current.selectedIndex;

    int bgR = isSelected ? 24 : 13;
    int bgG = isSelected ? 74 : 18;
    int bgB = isSelected ? 98 : 25;
    int textR = isSelected ? 255 : 215;
    int textG = isSelected ? 255 : 225;
    int textB = isSelected ? 255 : 235;

    DrawRect(menuX, currentY, menuWidth, itemHeight,
             bgR, bgG, bgB, isSelected ? 245 : 225);
    if (isSelected)
      DrawRect(menuX, currentY, 0.004f * menuScale, itemHeight,
               55, 205, 255, 255);
    DrawTextStr(item.name, menuX + 0.012f * menuScale,
                currentY + 0.006f * menuScale, 0.34f * menuScale, textR,
                textG, textB, 255);

    // Draw Value
    char valBuf[64]{};
    int valueR = textR;
    int valueG = textG;
    int valueB = textB;
    if (item.type == MenuItem::Bool && item.boolVal) {
      sprintf_s(valBuf, "%s", *item.boolVal ? "ON" : "OFF");
      valueR = *item.boolVal ? 75 : 255;
      valueG = *item.boolVal ? 235 : 95;
      valueB = *item.boolVal ? 125 : 90;
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
    } else if (item.type == MenuItem::Action) {
      sprintf_s(valBuf, "[ APPLY ]");
      valueR = 75;
      valueG = 210;
      valueB = 255;
    } else if (item.type == MenuItem::KeyBind && item.keyVal) {
      if (waitingForKeyBind && isSelected) {
        sprintf_s(valBuf, "[PRESS KEY]");
      } else {
        sprintf_s(valBuf, "[%s]", VkName(*item.keyVal).c_str());
      }
    }

    if (valBuf[0] != '\0') {
      DrawTextStr(valBuf, menuX + menuWidth - 0.058f * menuScale,
                  currentY + 0.006f * menuScale, 0.33f * menuScale,
                  valueR, valueG, valueB, 255, true);
    }

    currentY += itemHeight;
  }

  DrawRect(menuX, currentY, menuWidth, footerHeight,
           11, 19, 28, 255);
  char pageBuf[48]{};
  sprintf_s(pageBuf, "%d / %d", current.selectedIndex + 1, itemCount);
  DrawTextStr(pageBuf, menuX + 0.012f * menuScale,
              currentY + 0.009f * menuScale,
              0.27f * menuScale, 105, 195, 225, 235);
  DrawTextStr("ARROWS adjust   ENTER select   BACK close",
              menuX + menuWidth - 0.115f * menuScale,
              currentY + 0.009f * menuScale,
              0.24f * menuScale, 150, 165, 180, 225, true);
}
