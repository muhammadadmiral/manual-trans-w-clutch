#include "sdk/inc/main.h"
#include "sdk/inc/natives.h"
#include "src/Vehicle/VehicleData.h"
#include "src/Core/Config.h"

#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

HMODULE g_pluginModule = nullptr;

void ShowNotification(const char* message) {
    HUD::BEGIN_TEXT_COMMAND_THEFEED_POST("STRING");
    HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(message);
    HUD::END_TEXT_COMMAND_THEFEED_POST_TICKER(false, false);
}

void DrawTextOverlay(const char* text, float x, float y) {
    HUD::SET_TEXT_FONT(0);
    HUD::SET_TEXT_SCALE(0.42f, 0.42f);
    HUD::SET_TEXT_COLOUR(255, 255, 255, 255);
    HUD::SET_TEXT_OUTLINE();
    HUD::BEGIN_TEXT_COMMAND_DISPLAY_TEXT("STRING");
    HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(text);
    HUD::END_TEXT_COMMAND_DISPLAY_TEXT(x, y, 0);
}

bool IsValidVehicle(Vehicle vehicle) {
    const int vehicleClass = VEHICLE::GET_VEHICLE_CLASS(vehicle);
    if (vehicleClass == 14 || vehicleClass == 15 ||
        vehicleClass == 16 || vehicleClass == 21) {
        return false;
    }

    return VEHICLE::_GET_VEHICLE_MAX_DRIVE_GEAR_COUNT(vehicle) > 1;
}

bool IsPlayerDriving(Ped playerPed, Vehicle vehicle) {
    return VEHICLE::GET_PED_IN_VEHICLE_SEAT(vehicle, -1, 0) == playerPed;
}

void ResetInputEdges(bool& shiftUpPressed, bool& shiftDownPressed) {
    shiftUpPressed = false;
    shiftDownPressed = false;
}

} // namespace

void ScriptMain() {
    scriptWait(2000);

    if (!VehicleData::Initialize(g_pluginModule)) {
        ShowNotification(
            "Manual transmission disabled: CVehicle offsets unresolved.");
        return;
    }
    
    Config::ReadConfig(g_pluginModule);

    const VehicleOffsets& offsets = VehicleData::GetResolvedOffsets();
    char loadedMessage[192]{};
    sprintf_s(loadedMessage,
              "Manual transmission: %s | G:%X N:%X R:%X C:%X",
              VehicleData::GetOffsetSourceName(), offsets.Gear,
              offsets.NextGear, offsets.RPM, offsets.Clutch);
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

        const Vehicle vehicle =
            PED::GET_VEHICLE_PED_IS_IN(playerPed, FALSE);
        if (!IsValidVehicle(vehicle) ||
            !IsPlayerDriving(playerPed, vehicle)) {
            activeVehicle = 0;
            activeLayoutValidated = false;
            ResetInputEdges(shiftUpPressed, shiftDownPressed);
            continue;
        }

        const int maxGear =
            VEHICLE::_GET_VEHICLE_MAX_DRIVE_GEAR_COUNT(vehicle);
        VehicleData data(vehicle);

        if (vehicle != activeVehicle) {
            activeVehicle = vehicle;
            activeLayoutValidated = data.HasPlausibleLayout(maxGear);

            if (!activeLayoutValidated) {
                ShowNotification(
                    "CVehicle validation failed. Memory writes blocked.");
                ResetInputEdges(shiftUpPressed, shiftDownPressed);
                continue;
            }

            const uint8_t detectedGear = data.GetGear();
            manualGear = detectedGear >= 1 && detectedGear <= maxGear
                ? detectedGear
                : 1;

            shiftUpPressed =
                (GetAsyncKeyState(Config::KeyShiftUp) & 0x8000) != 0;
            shiftDownPressed =
                (GetAsyncKeyState(Config::KeyShiftDown) & 0x8000) != 0;
        }

        if (!activeLayoutValidated || !data.IsValid()) continue;

        const bool isUp =
            (GetAsyncKeyState(Config::KeyShiftUp) & 0x8000) != 0;
        const bool isDown =
            (GetAsyncKeyState(Config::KeyShiftDown) & 0x8000) != 0;
        const bool isClutch =
            (GetAsyncKeyState(Config::KeyClutch) & 0x8000) != 0;

        if (isUp && !shiftUpPressed && manualGear < maxGear) {
            ++manualGear;
        }
        if (isDown && !shiftDownPressed && manualGear > 0) {
            --manualGear;
        }

        shiftUpPressed = isUp;
        shiftDownPressed = isDown;

        // --- Clutch Logic ---
        if (isClutch) {
            VEHICLE::_SET_VEHICLE_ENGINE_TORQUE_MULTIPLIER(vehicle, 0.0f);
        } else {
            VEHICLE::_SET_VEHICLE_ENGINE_TORQUE_MULTIPLIER(vehicle, 1.0f);
        }

        const bool writeSucceeded =
            data.SetGear(manualGear) && data.SetNextGear(manualGear);
        if (!writeSucceeded) {
            activeLayoutValidated = false;
            ShowNotification("CVehicle write failed. Writes stopped.");
            continue;
        }

        if (Config::DebugOverlay) {
            char debugText[192]{};
            sprintf_s(debugText,
                      "Gear %u/%d | game %u -> %u | RPM %.3f | Clutch: %s",
                      static_cast<unsigned>(manualGear), maxGear,
                      static_cast<unsigned>(data.GetGear()),
                      static_cast<unsigned>(data.GetNextGear()),
                      data.GetRPM(), isClutch ? "ON" : "OFF");
            DrawTextOverlay(debugText, 0.02f, 0.80f);
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
