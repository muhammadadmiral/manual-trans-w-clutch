// =============================================================================
// MainScript.cpp — Orkestrator per-frame (v1.1)
// Rumus fisika dipindah ke Controllers, file ini murni sebagai wiring.
// =============================================================================
#define NOMINMAX
#include "MainScript.h"
#include "../Core/VersionInfo.h"

#include "../../sdk/inc/main.h"
#include "../../sdk/inc/natives.h"

#include "../Core/Config.h"
#include "../Core/InputHandler.h"
#include "../Core/Menu.h"
#include "../Core/ModLogger.h"
#include "../Core/Renderer.h"
#include "../Audio/AudioEngine.h"
#include "../Memory/GearboxPatches.h"
#include "../Vehicle/Maintenance/RefuelInteraction.h"
#include "../Vehicle/Maintenance/ServiceInteraction.h"
#include "../Vehicle/Maintenance/WorkshopIntegration.h"
#include "../Vehicle/Maintenance/WorkshopTuning.h"
#include "../Vehicle/Gearbox/Core/GearboxProfile.h"
#include "../Vehicle/VehicleData.h"
#include "../Vehicle/VehicleProfile.h"

#include "VehicleBlackboard.h"
#include "DrivingEventBus.h"
#include "Controllers/VehicleSessionController.h"
#include "Controllers/EngineController.h"
#include "Controllers/SignalController.h"
#include "Controllers/CalibrationController.h"
#include "Controllers/TransmissionController.h"
#include "Controllers/DriveAssistController.h"
#include "Controllers/DiagnosticsController.h"
#include "Controllers/HUDController.h"

#include <Windows.h>
#include <unordered_map>
#include <string>

extern HMODULE g_pluginModule;

namespace {

bool IsModelExcluded(int vehicleClass) {
    for (const int excl : Config::ExcludedVehicleClasses)
        if (excl == vehicleClass)
            return true;
    return false;
}

bool ComputeIsValidVehicle(Vehicle vehicle) {
    if (!vehicle || !ENTITY::DOES_ENTITY_EXIST(vehicle))
        return false;
    const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
    if (VEHICLE::IS_THIS_MODEL_A_PLANE(model) ||
        VEHICLE::IS_THIS_MODEL_A_HELI(model) ||
        VEHICLE::IS_THIS_MODEL_A_BOAT(model) ||
        VEHICLE::IS_THIS_MODEL_A_JETSKI(model) ||
        VEHICLE::IS_THIS_MODEL_A_TRAIN(model) ||
        VEHICLE::IS_THIS_MODEL_A_BICYCLE(model))
        return false;
    if (!Config::AllowQuadbikes && VEHICLE::IS_THIS_MODEL_A_QUADBIKE(model))
        return false;
    if (IsModelExcluded(VEHICLE::GET_VEHICLE_CLASS(vehicle)))
        return false;
    return true;
}

bool IsValidVehicle(Vehicle vehicle) {
    if (!vehicle || !ENTITY::DOES_ENTITY_EXIST(vehicle))
        return false;
    static std::unordered_map<Hash, bool> s_cache;
    const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
    const auto it = s_cache.find(model);
    if (it != s_cache.end())
        return it->second;
    const bool v = ComputeIsValidVehicle(vehicle);
    if (v)
        s_cache.emplace(model, true);
    return v;
}

bool IsPlayerDriving(Ped playerPed, Vehicle vehicle) {
    return VEHICLE::GET_PED_IN_VEHICLE_SEAT(vehicle, -1, 0) == playerPed;
}

} // namespace

// =============================================================================
// ScriptMain — the ScriptHookV game-loop thread
// =============================================================================
void ScriptMain() {
    LOG_INFO(Script, "%s started — waiting for player ped...", VersionInfo::kNotifyPrefix);

    // ── Phase 1: wait for the player to spawn ─────────────────────────────────
    while (true) {
        scriptWait(1000);
        const Ped p = PLAYER::PLAYER_PED_ID();
        if (ENTITY::DOES_ENTITY_EXIST(p) && !PED::IS_PED_INJURED(p))
            break;
    }
    LOG_INFO(Script, "Player ped ready. Initializing VehicleData...");

    // ── Phase 2: resolve CVehicle offsets ─────────────────────────────────────
    if (!VehicleData::Initialize(g_pluginModule)) {
        const std::string reason = VehicleData::GetLastFailureReason();
        LOG_FATAL(Script, "VehicleData::Initialize failed: %s", reason.c_str());
        Renderer::ShowNotification(("~r~" + std::string(VersionInfo::kBuildLabel) + " disabled: " + reason).c_str());
        return;
    }
    LOG_INFO(Script, "VehicleData::Initialize OK. Reading config...");

    Config::ReadConfig(g_pluginModule);
    GearboxProfile::Initialize(g_pluginModule);
    WorkshopTuning::Initialize(g_pluginModule);
    DrivingEventBus::Reset();
    DiagnosticsController::Initialize();
    AudioEngine::Initialize(g_pluginModule);
    ModLogger::SetMinLevel(Config::DebugOverlay ? ModLogger::Level::Verbose
                                                : ModLogger::Level::Info);
    LOG_INFO(Script, "Config loaded. Verbose logging: %s",
             Config::DebugOverlay ? "ON" : "off");

    // ── Initialize Controllers ────────────────────────────────────────────────
    VehicleSessionController sessionCtrl;
    EngineController         engineCtrl;
    SignalController         signalCtrl;
    CalibrationController    calibCtrl;
    TransmissionController   transCtrl;
    DriveAssistController    assistCtrl;

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (true) {
        scriptWait(0);
        DrivingEventBus::FlushFrame();

        // Force re-calibrate from menu
        if (Config::ForceRecalibrate) {
            Config::ForceRecalibrate = false;
            LOG_INFO(Calib, "ForceRecalibrate flag set — resetting calibration");
            VehicleData::ResetCalibration();
            calibCtrl.ResetLayout();
        }

        Menu::Update();
        AudioEngine::Update();

        const Ped playerPed = PLAYER::PLAYER_PED_ID();

        // Menu interactions (Service/Refuel)
        if (ServiceInteraction::IsActive()) {
            ServiceInteraction::Update(playerPed);
        } else {
            RefuelInteraction::Update(playerPed);
            const bool oilServiceRequested =
                (GetAsyncKeyState(Config::KeyOilService) & 0x8000) != 0;
            if (!RefuelInteraction::IsPromptVisible() || oilServiceRequested)
                ServiceInteraction::Update(playerPed);
        }

        // ── Not in a vehicle ──────────────────────────────────────────────────
        if (!PED::IS_PED_IN_ANY_VEHICLE(playerPed, FALSE)) {
            GearboxPatches::SetActive(false);
            const bool hadSession = sessionCtrl.GetActiveVehicle() != 0;
            sessionCtrl.Reset();
            engineCtrl.Reset();
            signalCtrl.Reset();
            transCtrl.Reset();
            if (hadSession) {
                assistCtrl.Reset();
                DiagnosticsController::Reset();
                DrivingEventBus::ClearPending();
            }
            InputHandler::ResetEdges();
            Menu::Draw();
            continue;
        }

        const Vehicle vehicle = PED::GET_VEHICLE_PED_IS_IN(playerPed, FALSE);
        if (!IsValidVehicle(vehicle) || !IsPlayerDriving(playerPed, vehicle)) {
            GearboxPatches::SetActive(false);
            const bool hadSession = sessionCtrl.GetActiveVehicle() != 0;
            sessionCtrl.Reset();
            engineCtrl.Reset();
            signalCtrl.Reset();
            transCtrl.Reset();
            if (hadSession) {
                assistCtrl.Reset();
                DiagnosticsController::Reset();
                DrivingEventBus::ClearPending();
            }
            InputHandler::ResetEdges();
            Menu::Draw();
            continue;
        }

        // ── In valid vehicle ──────────────────────────────────────────────────
        const int maxDriveGear = VEHICLE::GET_VEHICLE_HIGH_GEAR(vehicle);
        
        const bool isEngineOn =
            VEHICLE::GET_IS_VEHICLE_ENGINE_RUNNING(vehicle) != FALSE;

        // 1. Populate Blackboard
        g_frame.Populate(vehicle, playerPed, maxDriveGear);

        // 2. Handle session change
        if (sessionCtrl.CheckAndUpdate(vehicle, maxDriveGear)) {
            engineCtrl.Reset();
            signalCtrl.Reset();
            calibCtrl.ResetLayout();
            transCtrl.Reset();
            assistCtrl.Reset();
            DiagnosticsController::Reset();
            engineCtrl.Initialize(
                vehicle, g_frame.profile, Config::RequireColdStart,
                isEngineOn);
        }

        InputHandler::Update(transCtrl.GetManualGear());
        const float smoothThrottle = InputHandler::GetSmoothedThrottle();

        VehicleData data(vehicle);
        g_frame.UpdateRPM(data);

        // 3. Check calibration layout
        calibCtrl.CheckLayout(data, maxDriveGear);
        if (!calibCtrl.IsLayoutValid()) {
            // Calibration taking place
            calibCtrl.Update(vehicle, isEngineOn, smoothThrottle, maxDriveGear, data);
            Menu::Draw();
            continue;
        }

        // 4. Update core logic
        engineCtrl.Update(vehicle, g_frame.profile, isEngineOn,
                          transCtrl.GetManualGear());
        signalCtrl.Update(vehicle);

        const bool workshopOpen =
            WorkshopIntegration::Update(
                playerPed, vehicle, engineCtrl.IsOn());

        // 5. Run Drivetrain physics loop
        transCtrl.Update(vehicle, data, g_frame.profile, engineCtrl.IsOn(), workshopOpen,
                         g_frame.vehicleSpeed, g_frame.forwardSpeed,
                         maxDriveGear, assistCtrl);

        // 6. Handle drivetrain stall request
        if (transCtrl.ConsumedStallEvent()) {
            engineCtrl.ForceStall(vehicle, "drivetrain");
        }

        // 7. Telemetry & Diagnostics
        DiagnosticsController::Update(
            vehicle, data, g_frame.profile, transCtrl.GetManualGear(),
            transCtrl.GetMode(), transCtrl.GetDriveThrottle(), transCtrl.GetBrake(),
            transCtrl.GetSimulatedClutch(), g_frame.speedKmH, g_frame.forwardSpeed,
            engineCtrl.IsOn(), engineCtrl.IsStarting(), transCtrl.GetMode() == 1,
            assistCtrl
        );

        // 8. Render HUD
        HUDController::Update(
            vehicle, data, g_frame.profile, transCtrl.GetManualGear(), maxDriveGear,
            transCtrl.GetMode(), transCtrl.GetSimulatedClutch(), transCtrl.GetDriveThrottle(),
            transCtrl.GetBrake(), g_frame.speedKmH, g_frame.rpm, engineCtrl.IsOn(),
            engineCtrl.IsStarting(), transCtrl.GetMode() == 1, signalCtrl.GetActiveSignal(),
            transCtrl.GetGrindWarningTimer(), assistCtrl
        );

        Menu::Draw();
    }
}
