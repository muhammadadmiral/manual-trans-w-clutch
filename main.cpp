#include "sdk/inc/main.h"
#include "sdk/inc/natives.h"
#include "src/VehicleData.h"
#include <stdio.h>
#include <windows.h>

void ShowNotification(const char *message) {
  HUD::BEGIN_TEXT_COMMAND_THEFEED_POST("STRING");
  HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(message);
  HUD::END_TEXT_COMMAND_THEFEED_POST_TICKER(false, false);
}

void DrawTextOverlay(const char *text, float x, float y) {
  HUD::SET_TEXT_FONT(0);
  HUD::SET_TEXT_SCALE(0.5f, 0.5f);
  HUD::SET_TEXT_COLOUR(255, 255, 255, 255);
  HUD::SET_TEXT_OUTLINE();
  HUD::BEGIN_TEXT_COMMAND_DISPLAY_TEXT("STRING");
  HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(text);
  HUD::END_TEXT_COMMAND_DISPLAY_TEXT(x, y, 0);
}

bool IsValidVehicle(Vehicle veh) {
  int vehClass = VEHICLE::GET_VEHICLE_CLASS(veh);
  if (vehClass == 14 || vehClass == 15 || vehClass == 16 || vehClass == 21) {
    return false;
  }
  int maxGear = VEHICLE::_GET_VEHICLE_MAX_DRIVE_GEAR_COUNT(veh);
  if (maxGear <= 1) {
    return false;
  }
  return true;
}

void ScriptMain() {
  scriptWait(2000);
  ShowNotification("Manual Transmission Mod Loaded!");

  char iniPath[MAX_PATH];
  GetCurrentDirectoryA(MAX_PATH, iniPath);
  strcat_s(iniPath, "\\manual-trans.ini");

  int keyShiftUp =
      GetPrivateProfileIntA("Controls", "ShiftUp", VK_LSHIFT, iniPath);
  int keyShiftDown =
      GetPrivateProfileIntA("Controls", "ShiftDown", VK_LCONTROL, iniPath);

  uint8_t manualGear = 1;
  bool shiftUpPressed = false;
  bool shiftDownPressed = false;

  while (true) {
    scriptWait(0);
    Ped playerPed = PLAYER::PLAYER_PED_ID();

    if (PED::IS_PED_IN_ANY_VEHICLE(playerPed, FALSE)) {
      Vehicle veh = PED::GET_VEHICLE_PED_IS_IN(playerPed, FALSE);

      if (IsValidVehicle(veh)) {
        VehicleData vData(veh);
        if (vData.IsValid()) {
          int maxGear = VEHICLE::_GET_VEHICLE_MAX_DRIVE_GEAR_COUNT(veh);

          bool isUp = (GetAsyncKeyState(keyShiftUp) & 0x8000) != 0;
          bool isDown = (GetAsyncKeyState(keyShiftDown) & 0x8000) != 0;

          if (isUp && !shiftUpPressed) {
            if (manualGear < maxGear)
              manualGear++;
          }
          if (isDown && !shiftDownPressed) {
            if (manualGear > 0)
              manualGear--;
          }

          shiftUpPressed = isUp;
          shiftDownPressed = isDown;

          vData.SetGear(manualGear);
          vData.SetNextGear(manualGear);

          char debugText[128];
          sprintf_s(debugText, "Gear: %d | RPM: %.2f", manualGear,
                    vData.GetRPM());
          DrawTextOverlay(debugText, 0.1f, 0.8f);
        }
      }
    }
  }
}

BOOL APIENTRY DllMain(HMODULE hInstance, DWORD reason, LPVOID lpReserved) {
  switch (reason) {
  case DLL_PROCESS_ATTACH:
    DisableThreadLibraryCalls(hInstance);
    scriptRegister(hInstance, ScriptMain);
    break;
  case DLL_PROCESS_DETACH:
    scriptUnregister(hInstance);
    break;
  }
  return TRUE;
}