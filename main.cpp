#include "sdk/inc/main.h"
#include "sdk/inc/natives.h"
#include <windows.h>
#include <stdio.h>
#include "src/VehicleData.h"

void ShowNotification(const char *message) {
  HUD::BEGIN_TEXT_COMMAND_THEFEED_POST("STRING");
  HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(message);
  HUD::END_TEXT_COMMAND_THEFEED_POST_TICKER(false, false);
}

void DrawTextOverlay(const char* text, float x, float y) {
  HUD::SET_TEXT_FONT(0);
  HUD::SET_TEXT_SCALE(0.5f, 0.5f);
  HUD::SET_TEXT_COLOUR(255, 255, 255, 255);
  HUD::SET_TEXT_OUTLINE();
  HUD::BEGIN_TEXT_COMMAND_DISPLAY_TEXT("STRING");
  HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(text);
  HUD::END_TEXT_COMMAND_DISPLAY_TEXT(x, y, 0);
}

void ScriptMain() {
  scriptWait(2000);
  ShowNotification("Manual Transmission Mod Loaded!");
  while (true) {
    scriptWait(0);
    Ped playerPed = PLAYER::PLAYER_PED_ID();
    if (PED::IS_PED_IN_ANY_VEHICLE(playerPed, FALSE)) {
      Vehicle veh = PED::GET_VEHICLE_PED_IS_IN(playerPed, FALSE);
      VehicleData vData(veh);
      
      if (vData.IsValid()) {
          char debugText[128];
          sprintf_s(debugText, "Gear: %d | RPM: %.2f", vData.GetGear(), vData.GetRPM());
          DrawTextOverlay(debugText, 0.1f, 0.8f);
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