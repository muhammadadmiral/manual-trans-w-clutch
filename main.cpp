#define NOMINMAX
#include "sdk/inc/main.h"
#include "sdk/inc/natives.h"
#include "src/Core/Config.h"
#include "src/Core/Menu.h"
#include "src/Vehicle/VehicleData.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

HMODULE g_pluginModule = nullptr;

namespace {

constexpr int kInputGroupVehicle = 0;
constexpr int kControlVehicleAccelerate = 71;
constexpr int kControlVehicleBrake = 72;

float GetControlNormal(int inputGroup, int control) {
  return PAD::GET_CONTROL_NORMAL(inputGroup, control);
}

void ShowNotification(const char *message) {
  HUD::BEGIN_TEXT_COMMAND_THEFEED_POST("STRING");
  HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(message);
  HUD::END_TEXT_COMMAND_THEFEED_POST_TICKER(false, false);
}

void DrawTextOverlay(const char *text, float x, float y, float scale = 0.42f,
                     int r = 255, int g = 255, int b = 255, int a = 255) {
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

bool IsVehicleClassExcluded(int vehicleClass) {
  for (const int excluded : Config::ExcludedVehicleClasses) {
    if (excluded == vehicleClass)
      return true;
  }
  return false;
}

bool ComputeIsValidVehicle(Vehicle vehicle) {
  if (vehicle == 0 || !ENTITY::DOES_ENTITY_EXIST(vehicle))
    return false;

  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);

  // Exclude air, water, train, bicycle
  if (VEHICLE::IS_THIS_MODEL_A_PLANE(model) ||
      VEHICLE::IS_THIS_MODEL_A_HELI(model) ||
      VEHICLE::IS_THIS_MODEL_A_BOAT(model) ||
      VEHICLE::IS_THIS_MODEL_A_JETSKI(model) ||
      VEHICLE::IS_THIS_MODEL_A_TRAIN(model) ||
      VEHICLE::IS_THIS_MODEL_A_BICYCLE(model)) {
    return false;
  }

  if (!Config::AllowQuadbikes && VEHICLE::IS_THIS_MODEL_A_QUADBIKE(model)) {
    return false;
  }

  if (IsVehicleClassExcluded(VEHICLE::GET_VEHICLE_CLASS(vehicle))) {
    return false;
  }

  return true;
}

bool IsValidVehicle(Vehicle vehicle) {
  if (vehicle == 0 || !ENTITY::DOES_ENTITY_EXIST(vehicle))
    return false;

  static std::unordered_map<Hash, bool> classificationCache;
  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
  const auto cached = classificationCache.find(model);
  if (cached != classificationCache.end())
    return cached->second;

  const bool valid = ComputeIsValidVehicle(vehicle);
  if (valid) {
    classificationCache.emplace(model, true);
  }
  return valid;
}

bool IsPlayerDriving(Ped playerPed, Vehicle vehicle) {
  return VEHICLE::GET_PED_IN_VEHICLE_SEAT(vehicle, -1, 0) == playerPed;
}

void ResetInputEdges(bool &shiftUpPressed, bool &shiftDownPressed) {
  shiftUpPressed = false;
  shiftDownPressed = false;
}

void PlayGearGrindSound(Vehicle vehicle) {
  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
  if (VEHICLE::IS_THIS_MODEL_A_BIKE(model) ||
      VEHICLE::IS_THIS_MODEL_A_QUADBIKE(model)) {
    AUDIO::PLAY_SOUND_FROM_ENTITY(-1, "NAV_UP_DOWN", vehicle,
                                  "HUD_FREEMODE_SOUNDSET", 0, 0);
  } else {
    AUDIO::PLAY_SOUND_FROM_ENTITY(-1, "BAR_OUT_OF_RANGE", vehicle,
                                  "HUD_MINIGAME_SOUNDSET", 0, 0);
  }
}

void PlayGearShiftSound() {
  AUDIO::PLAY_SOUND_FRONTEND(-1, "NAV_LEFT_RIGHT",
                             "HUD_FRONTEND_DEFAULT_SOUNDSET", 1);
}

void DrawGearHUD(int manualGear, int maxGear, float speedKmH) {
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

  // Speed Label below badge
  char speedStr[32]{};
  sprintf_s(speedStr, "%d KM/H", static_cast<int>(speedKmH));
  DrawTextOverlay(speedStr, badgeX + 0.008f, badgeY + badgeH + 0.004f, 0.32f,
                  220, 220, 220, 230);
}

} // namespace

void ScriptMain() {
  // Wait until game loading completes and player ped exists
  while (true) {
    scriptWait(1000);
    const Ped playerPed = PLAYER::PLAYER_PED_ID();
    if (ENTITY::DOES_ENTITY_EXIST(playerPed) &&
        !PED::IS_PED_INJURED(playerPed)) {
      break;
    }
  }

  if (!VehicleData::Initialize(g_pluginModule)) {
    const std::string buildVersion = VehicleData::GetGameBuildVersion();
    char failMessage[256]{};
    sprintf_s(failMessage, "Manual transmission disabled (build %s): %s",
              buildVersion.empty() ? "unknown" : buildVersion.c_str(),
              VehicleData::GetLastFailureReason());
    ShowNotification(failMessage);
    return;
  }

  Config::ReadConfig(g_pluginModule);

  bool notificationShown = false;
  Vehicle activeVehicle = 0;

  // manualGear: -1 = R (Reverse), 0 = N (Neutral), 1..maxGear = Forward Gears
  int manualGear = 0;
  bool activeLayoutValidated = false;
  bool shiftUpPressed = false;
  bool shiftDownPressed = false;

  bool engineOnEdges = false;
  bool isEngineOn = true;

  float smoothedThrottle = 0.0f;
  float smoothedBrake = 0.0f;
  float smoothedClutch = 0.0f;

  int grindWarningTimer = 0;

  while (true) {
    scriptWait(0);
    Menu::Update();

    const Ped playerPed = PLAYER::PLAYER_PED_ID();
    if (!PED::IS_PED_IN_ANY_VEHICLE(playerPed, FALSE)) {
      activeVehicle = 0;
      activeLayoutValidated = false;
      ResetInputEdges(shiftUpPressed, shiftDownPressed);
      engineOnEdges = false;
      Menu::Draw();
      continue;
    }

    const Vehicle vehicle = PED::GET_VEHICLE_PED_IS_IN(playerPed, FALSE);
    if (!IsValidVehicle(vehicle) || !IsPlayerDriving(playerPed, vehicle)) {
      activeVehicle = 0;
      activeLayoutValidated = false;
      ResetInputEdges(shiftUpPressed, shiftDownPressed);
      Menu::Draw();
      continue;
    }

    // Show loaded notification once when entering a vehicle safely
    if (!notificationShown && VehicleData::IsInitialized()) {
      notificationShown = true;
      const VehicleOffsets &offsets = VehicleData::GetResolvedOffsets();
      const std::string buildVersion = VehicleData::GetGameBuildVersion();
      char loadedMessage[256]{};
      sprintf_s(loadedMessage,
                "Manual transmission: %s | build %s | G:%X N:%X R:%X C:%X",
                VehicleData::GetOffsetSourceName(),
                buildVersion.empty() ? "unknown" : buildVersion.c_str(),
                offsets.Gear, offsets.NextGear, offsets.RPM, offsets.Clutch);
      ShowNotification(loadedMessage);
    }

    const int maxGear = VEHICLE::_GET_VEHICLE_MAX_DRIVE_GEAR_COUNT(vehicle);
    VehicleData data(vehicle);
    isEngineOn = VEHICLE::GET_IS_VEHICLE_ENGINE_RUNNING(vehicle) != 0;

    if (vehicle != activeVehicle) {
      activeVehicle = vehicle;
      activeLayoutValidated = data.HasPlausibleLayout(maxGear > 0 ? maxGear : 6);

      if (Config::RequireColdStart) {
        VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
        isEngineOn = false;
      }

      if (!activeLayoutValidated && VehicleData::IsInitialized()) {
        ShowNotification("CVehicle validation failed. Recalibrating...");
        ResetInputEdges(shiftUpPressed, shiftDownPressed);
        VehicleData::ResetCalibration();
      } else {
        // Default entering vehicle to Neutral (N)
        manualGear = 0;
      }
      shiftUpPressed = (GetAsyncKeyState(Config::KeyShiftUp) & 0x8000) != 0;
      shiftDownPressed = (GetAsyncKeyState(Config::KeyShiftDown) & 0x8000) != 0;
      engineOnEdges = (GetAsyncKeyState(Config::KeyEngine) & 0x8000) != 0;
    }

    const bool engineKey = (GetAsyncKeyState(Config::KeyEngine) & 0x8000) != 0;
    if (engineKey && !engineOnEdges) {
      isEngineOn = !isEngineOn;
      VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, isEngineOn ? TRUE : FALSE, FALSE,
                                     TRUE);
    }
    engineOnEdges = engineKey;

    if (!VehicleData::IsInitialized()) {
      const bool isRevving = GetControlNormal(kInputGroupVehicle,
                                              kControlVehicleAccelerate) > 0.5f;
      VehicleData::UpdateCalibration(g_pluginModule, vehicle, isEngineOn,
                                     isRevving);

      if (VehicleData::IsInitialized()) {
        activeLayoutValidated = data.HasPlausibleLayout(maxGear > 0 ? maxGear : 6);
        ShowNotification("Calibration complete! Manual transmission active.");
      } else {
        CalibrationState state = VehicleData::GetCalibrationState();
        std::string calibMsg = "Calibration: ";
        switch (state) {
        case CalibrationState::WaitingForEngineOff:
          calibMsg += "Turn off engine (press " +
                      std::string(1, (char)Config::KeyEngine) + ")";
          break;
        case CalibrationState::ScanningEngineOff:
          calibMsg += "Scanning...";
          break;
        case CalibrationState::WaitingForEngineOn:
          calibMsg += "Turn ON engine and idle";
          break;
        case CalibrationState::ScanningEngineOn:
          calibMsg += "Scanning...";
          break;
        case CalibrationState::WaitingForRev:
          calibMsg += "Rev the engine (Hold W)";
          break;
        case CalibrationState::ScanningRev:
          calibMsg += "Scanning...";
          break;
        case CalibrationState::Done:
          calibMsg += "Success! Offsets saved.";
          break;
        case CalibrationState::Failed:
          calibMsg += "Failed. Try again.";
          break;
        default:
          break;
        }
        DrawTextOverlay(calibMsg.c_str(), 0.5f, 0.1f, 0.6f);
        Menu::Draw();
        continue;
      }
    }

    if (!activeLayoutValidated || !data.IsValid()) {
      Menu::Draw();
      continue;
    }

    const bool isUp = (GetAsyncKeyState(Config::KeyShiftUp) & 0x8000) != 0;
    const bool isDown = (GetAsyncKeyState(Config::KeyShiftDown) & 0x8000) != 0;
    const bool isClutch = (GetAsyncKeyState(Config::KeyClutch) & 0x8000) != 0;

    // --- Analog Emulation / Smoothing ---
    const bool isThrottle = (GetAsyncKeyState(0x57) & 0x8000) != 0 ||
                            (GetAsyncKeyState(VK_UP) & 0x8000) != 0; // W or UP
    const bool isBrake = (GetAsyncKeyState(0x53) & 0x8000) != 0 ||
                         (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0; // S or DOWN

    if (isThrottle) {
      smoothedThrottle += Config::ThrottleAttack;
    } else {
      smoothedThrottle -= Config::ThrottleRelease;
    }

    if (isBrake) {
      smoothedBrake += Config::BrakeAttack;
    } else {
      smoothedBrake -= Config::BrakeRelease;
    }

    if (isClutch) {
      smoothedClutch += Config::ClutchAttack;
    } else {
      smoothedClutch -= Config::ClutchRelease;
    }

    smoothedThrottle = (std::max)(0.0f, (std::min)(1.0f, smoothedThrottle));
    smoothedBrake = (std::max)(0.0f, (std::min)(1.0f, smoothedBrake));
    smoothedClutch = (std::max)(0.0f, (std::min)(1.0f, smoothedClutch));

    PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, kControlVehicleAccelerate,
                                      smoothedThrottle);
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, kControlVehicleBrake, smoothedBrake);

    const float vehicleSpeed = ENTITY::GET_ENTITY_SPEED(vehicle); // m/s
    const float speedKmH = vehicleSpeed * 3.6f;

    // --- Gear Shift Logic & Clutch Check ("Gredek" / Grind sound) ---
    if (isUp && !shiftUpPressed) {
      if (smoothedClutch < 0.35f && isEngineOn) {
        // Clutch not pressed: Gear Shift BLOCKED + Grind Sound!
        PlayGearGrindSound(vehicle);
        grindWarningTimer = 45;
      } else if (manualGear < maxGear) {
        ++manualGear;
        PlayGearShiftSound();
      }
    }

    if (isDown && !shiftDownPressed) {
      if (smoothedClutch < 0.35f && isEngineOn) {
        // Clutch not pressed: Gear Shift BLOCKED + Grind Sound!
        PlayGearGrindSound(vehicle);
        grindWarningTimer = 45;
      } else if (manualGear > -1) {
        --manualGear;
        PlayGearShiftSound();
      }
    }

    shiftUpPressed = isUp;
    shiftDownPressed = isDown;

    // --- Realistic Engine Stall Logic ---
    // Engine stalls ONLY if in gear (manualGear != 0), vehicle is nearly stopped (< 1.5m/s),
    // clutch is not pressed (< 0.25f), and player is not applying throttle / RPM is low.
    if (isEngineOn && manualGear != 0) {
      if (vehicleSpeed < 1.5f && smoothedClutch < 0.25f &&
          smoothedThrottle < 0.15f && data.GetRPM() < 0.22f) {
        isEngineOn = false;
        VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
        ShowNotification("Engine Stalled! (Depress clutch or shift to Neutral N)");
        AUDIO::PLAY_SOUND_FROM_ENTITY(-1, "BAR_OUT_OF_RANGE", vehicle,
                                      "HUD_MINIGAME_SOUNDSET", 0, 0);
      }
    }

    // --- Drive Power & Memory Synchronization (Fixes Rapid Gear Crash) ---
    if (manualGear == 0) {
      // NEUTRAL (N): Keep actual memory gear at 1 (or current), but zero drive power!
      data.SetGear(1);
      data.SetNextGear(1);
      VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(vehicle, 0.0f);
      data.SetClutch(0.0f);
    } else if (manualGear == -1) {
      // REVERSE (R): Memory gear 0 represents Reverse in GTA V.
      data.SetGear(0);
      data.SetNextGear(0);
      const float drivePower = (std::max)(0.0f, 1.0f - smoothedClutch);
      VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(vehicle, drivePower);
      data.SetClutch(smoothedClutch);
    } else {
      // FORWARD GEARS (1..maxGear)
      const uint8_t targetGear = static_cast<uint8_t>(manualGear);
      data.SetGear(targetGear);
      data.SetNextGear(targetGear);
      const float drivePower = (std::max)(0.0f, 1.0f - smoothedClutch);
      VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(vehicle, drivePower);
      data.SetClutch(smoothedClutch);
    }

    // --- HUD & Overlay Rendering ---
    DrawGearHUD(manualGear, maxGear, speedKmH);

    if (grindWarningTimer > 0) {
      --grindWarningTimer;
      DrawTextOverlay("CLUTCH REQUIRED! (Gears Grinding)", 0.38f, 0.75f, 0.55f,
                      255, 60, 60, 255);
    }

    if (Config::DebugOverlay) {
      char debugText[256]{};
      sprintf_s(
          debugText,
          "ModGear: %d | game: %u -> %u | RPM: %.3f | Clutch: %.2f | Src: %s",
          manualGear, static_cast<unsigned>(data.GetGear()),
          static_cast<unsigned>(data.GetNextGear()), data.GetRPM(),
          data.GetClutch(), VehicleData::GetOffsetSourceName());
      DrawTextOverlay(debugText, 0.02f, 0.95f, 0.38f);
    }

    if (Config::OverlayBars) {
      const float barX = Config::OverlayPosX;
      const float barWidth = Config::OverlayBarWidth;
      const float barHeight = Config::OverlayBarHeight;
      const float gap = barHeight + 0.006f;
      float y = Config::OverlayPosY;

      const float rpmFraction = data.GetRPM();
      const float clutchFraction = smoothedClutch;
      const float throttleFraction = smoothedThrottle;
      const float brakeFraction = smoothedBrake;

      DrawBar(barX, y, barWidth, barHeight, rpmFraction, 220, 60, 60, "RPM");
      y += gap;
      DrawBar(barX, y, barWidth, barHeight, clutchFraction, 220, 180, 60,
              "CLUTCH");
      y += gap;
      DrawBar(barX, y, barWidth, barHeight, throttleFraction, 80, 200, 90,
              "THROTTLE");
      y += gap;
      DrawBar(barX, y, barWidth, barHeight, brakeFraction, 220, 90, 220,
              "BRAKE");
    }

    Menu::Draw();
  }
}

BOOL APIENTRY DllMain(HMODULE instance, DWORD reason, LPVOID) {
  switch (reason) {
  case DLL_PROCESS_ATTACH:
    g_pluginModule = instance;
    DisableThreadLibraryCalls(instance);
    scriptRegister(instance, ScriptMain);
    break;
  case DLL_PROCESS_DETACH:
    scriptUnregister(instance);
    break;
  default:
    break;
  }

  return TRUE;
}