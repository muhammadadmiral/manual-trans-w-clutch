#include "sdk/inc/main.h"
#include "sdk/inc/natives.h"
#include <windows.h>

void ShowNotification(const char *message) {
  HUD::BEGIN_TEXT_COMMAND_THEFEED_POST("STRING");
  HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(message);
  HUD::END_TEXT_COMMAND_THEFEED_POST_TICKER(false, false);
}

void ScriptMain() {
  scriptWait(2000);
  ShowNotification("Manual Transmission Mod Loaded!");
  while (true) {
    scriptWait(0);
    Ped playerPed = PLAYER::PLAYER_PED_ID();
    if (PED::IS_PED_IN_ANY_VEHICLE(playerPed, FALSE)) {
      Vehicle veh = PED::GET_VEHICLE_PED_IS_IN(playerPed, FALSE);
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