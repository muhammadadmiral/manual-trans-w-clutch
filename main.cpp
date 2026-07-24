#include "sdk/inc/main.h"
#include "sdk/inc/natives.h"
#include "src/Core/Config.h"
#include "src/Vehicle/VehicleData.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace {

HMODULE g_pluginModule = nullptr;

// Standard eControl ids used across the GTA IV/V modding community since
// forever (INPUT_VEHICLE_ACCELERATE / INPUT_VEHICLE_BRAKE). If your
// generated natives.h groups GET_CONTROL_NORMAL under a different
// namespace than PAD:: (some newer/Enhanced native DBs use CONTROLS::),
// just change the qualifier in GetControlNormal() below - the rest of
// the file doesn't need to know.
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

void DrawTextOverlay(const char *text, float x, float y, float scale = 0.42f) {
  HUD::SET_TEXT_FONT(0);
  HUD::SET_TEXT_SCALE(scale, scale);
  HUD::SET_TEXT_COLOUR(255, 255, 255, 255);
  HUD::SET_TEXT_OUTLINE();
  HUD::BEGIN_TEXT_COMMAND_DISPLAY_TEXT("STRING");
  HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(text);
  HUD::END_TEXT_COMMAND_DISPLAY_TEXT(x, y, 0);
}

// Draws a simple filled bar using GRAPHICS::DRAW_RECT - no textures, no
// extra dependencies to ship. (x, y) is the top-left corner; DRAW_RECT
// itself positions by center, which is why the math below re-centers.
// The (x, y, width, height, r, g, b, a) 8-argument form has been stable
// for a decade of community native DBs; if your generated header adds a
// trailing bool/argument, the compiler will point at the exact line and
// it's a one-argument fix.
void DrawBar(float x, float y, float width, float height, float fraction, int r,
             int g, int b, const char *label) {
  fraction = (std::max)(0.0f, (std::min)(1.0f, fraction));

  GRAPHICS::DRAW_RECT(x + width * 0.5f, y + height * 0.5f, width, height, 20,
                      20, 20, 180);
  if (fraction > 0.0f) {
    const float fillWidth = width * fraction;
    GRAPHICS::DRAW_RECT(x + fillWidth * 0.5f, y + height * 0.5f, fillWidth,
                        height, r, g, b, 230);
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

// The actual classification logic. Kept separate from IsValidVehicle()
// below so the cache wrapper stays trivial to read.
bool ComputeIsValidVehicle(Vehicle vehicle) {
  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);

  // Vehicle types that never have a player-shiftable manual gearbox.
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

  // Optional, ini-configurable extra exclusions by VEHICLE::GET_VEHICLE_
  // CLASS() id - handy if you find a vehicle type the checks above miss.
  if (IsVehicleClassExcluded(VEHICLE::GET_VEHICLE_CLASS(vehicle))) {
    return false;
  }

  // Vehicles that only have a single drive gear are effectively automatic
  // (mopeds, some quads, etc.) - there's nothing to shift manually, so
  // this is what actually auto-detects "matic" cars/bikes.
  return VEHICLE::_GET_VEHICLE_MAX_DRIVE_GEAR_COUNT(vehicle) > 1;
}

// Vehicle class / gear count are static per-model in stock GTA V (they
// come from vehicles.meta/handling.meta, not per-instance state), so the
// result is cached by model hash to avoid re-running six natives every
// single tick for every vehicle the player might be in. If you run a mod
// that changes gear counts per-instance, key this by vehicle handle
// instead and accept the extra native calls.
bool IsValidVehicle(Vehicle vehicle) {
  static std::unordered_map<Hash, bool> classificationCache;

  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
  const auto cached = classificationCache.find(model);
  if (cached != classificationCache.end())
    return cached->second;

  const bool valid = ComputeIsValidVehicle(vehicle);
  classificationCache.emplace(model, valid);
  return valid;
}

bool IsPlayerDriving(Ped playerPed, Vehicle vehicle) {
  return VEHICLE::GET_PED_IN_VEHICLE_SEAT(vehicle, -1, 0) == playerPed;
}

void ResetInputEdges(bool &shiftUpPressed, bool &shiftDownPressed) {
  shiftUpPressed = false;
  shiftDownPressed = false;
}

} // namespace

void ScriptMain() {
  scriptWait(2000);

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

  const VehicleOffsets &offsets = VehicleData::GetResolvedOffsets();
  const std::string buildVersion = VehicleData::GetGameBuildVersion();
  char loadedMessage[256]{};
  sprintf_s(loadedMessage,
            "Manual transmission: %s | build %s | G:%X N:%X R:%X C:%X",
            VehicleData::GetOffsetSourceName(),
            buildVersion.empty() ? "unknown" : buildVersion.c_str(),
            offsets.Gear, offsets.NextGear, offsets.RPM, offsets.Clutch);
  ShowNotification(loadedMessage);

  Vehicle activeVehicle = 0;
  uint8_t manualGear = 1;
  bool activeLayoutValidated = false;
  bool shiftUpPressed = false;
  bool shiftDownPressed = false;

  while (true) {
    scriptWait(0);

    const Ped playerPed = PLAYER::PLAYER_PED_ID();
    if (!PED::IS_PED_IN_ANY_VEHICLE(playerPed, FALSE)) {
      activeVehicle = 0;
      activeLayoutValidated = false;
      ResetInputEdges(shiftUpPressed, shiftDownPressed);
      continue;
    }

    const Vehicle vehicle = PED::GET_VEHICLE_PED_IS_IN(playerPed, FALSE);
    if (!IsValidVehicle(vehicle) || !IsPlayerDriving(playerPed, vehicle)) {
      activeVehicle = 0;
      activeLayoutValidated = false;
      ResetInputEdges(shiftUpPressed, shiftDownPressed);
      continue;
    }

    const int maxGear = VEHICLE::_GET_VEHICLE_MAX_DRIVE_GEAR_COUNT(vehicle);
    VehicleData data(vehicle);

    if (vehicle != activeVehicle) {
      activeVehicle = vehicle;
      activeLayoutValidated = data.HasPlausibleLayout(maxGear);

      if (!activeLayoutValidated) {
        ShowNotification("CVehicle validation failed. Memory writes blocked.");
        ResetInputEdges(shiftUpPressed, shiftDownPressed);
        continue;
      }

      const uint8_t detectedGear = data.GetGear();
      manualGear =
          detectedGear >= 1 && detectedGear <= maxGear ? detectedGear : 1;

      shiftUpPressed = (GetAsyncKeyState(Config::KeyShiftUp) & 0x8000) != 0;
      shiftDownPressed = (GetAsyncKeyState(Config::KeyShiftDown) & 0x8000) != 0;
    }

    if (!activeLayoutValidated || !data.IsValid())
      continue;

    const bool isUp = (GetAsyncKeyState(Config::KeyShiftUp) & 0x8000) != 0;
    const bool isDown = (GetAsyncKeyState(Config::KeyShiftDown) & 0x8000) != 0;
    const bool isClutch = (GetAsyncKeyState(Config::KeyClutch) & 0x8000) != 0;

    if (isUp && !shiftUpPressed && manualGear < maxGear) {
      ++manualGear;
    }
    if (isDown && !shiftDownPressed && manualGear > 0) {
      --manualGear;
    }

    shiftUpPressed = isUp;
    shiftDownPressed = isDown;

    // --- Clutch Logic ---
    // Prefer writing the real clutch value (0 = engaged, 1 = disengaged) so
    // RPM/wheelspin behaves like an actual clutch. If that offset isn't
    // writable for some reason, fall back to the power-cheat trick, which is
    // cruder (kills power outright) but still stops the car from lurching.
    const float clutchTarget = isClutch ? 1.0f : 0.0f;
    if (Config::UseRealClutch && data.SetClutch(clutchTarget)) {
      VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(vehicle, 1.0f);
    } else if (isClutch) {
      VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(vehicle, 0.0f);
    } else {
      VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(vehicle, 1.0f);
    }

    const bool writeSucceeded =
        data.SetGear(manualGear) && data.SetNextGear(manualGear);
    if (!writeSucceeded) {
      activeLayoutValidated = false;
      ShowNotification("CVehicle write failed. Writes stopped.");
      continue;
    }

    if (Config::DebugOverlay) {
      char debugText[256]{};
      sprintf_s(
          debugText,
          "Gear %u/%d | game %u -> %u | RPM %.3f | Clutch %.2f (%s) | Src: %s",
          static_cast<unsigned>(manualGear), maxGear,
          static_cast<unsigned>(data.GetGear()),
          static_cast<unsigned>(data.GetNextGear()), data.GetRPM(),
          data.GetClutch(), isClutch ? "held" : "released",
          VehicleData::GetOffsetSourceName());
      DrawTextOverlay(debugText, 0.02f, 0.80f);
    }

    // --- Bar overlay: RPM / clutch (read from memory) and throttle /
    // brake (read from player input - no extra offsets needed for those
    // two at all). ---
    if (Config::OverlayBars) {
      const float barX = Config::OverlayPosX;
      const float barWidth = Config::OverlayBarWidth;
      const float barHeight = Config::OverlayBarHeight;
      const float gap = barHeight + 0.006f;
      float y = Config::OverlayPosY;

      const float rpmFraction = data.GetRPM();
      const float clutchFraction = data.GetClutch();
      const float throttleFraction =
          GetControlNormal(kInputGroupVehicle, kControlVehicleAccelerate);
      const float brakeFraction =
          GetControlNormal(kInputGroupVehicle, kControlVehicleBrake);

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