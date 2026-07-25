// =============================================================================
// MainScript.cpp
// ScriptHookV main game-loop script for manual-trans-w-clutch.
//
// This file owns:
//   - Vehicle validation helpers (anonymous namespace)
//   - ScriptMain() — the per-frame update loop
//
// It does NOT own DllMain() — that lives in src/Mod/DllMain.cpp.
// =============================================================================
#define NOMINMAX
#include "MainScript.h"

#include "../../sdk/inc/main.h"
#include "../../sdk/inc/natives.h"

#include "../Core/Config.h"
#include "../Core/InputHandler.h"
#include "../Core/Menu.h"
#include "../Core/ModLogger.h"
#include "../Core/Renderer.h"
#include "../Vehicle/FuelSystem.h"
#include "../Vehicle/GearLogic.h"
#include "../Vehicle/LightsLogic.h"
#include "../Vehicle/ParkingBrake.h"
#include "../Vehicle/PhysicsEngine.h"
#include "../Vehicle/TelemetryLogger.h"
#include "../Vehicle/TractionControl.h"
#include "../Vehicle/TurboSystem.h"
#include "../Vehicle/VehicleData.h"

#include <Windows.h>
#include <string>
#include <unordered_map>

extern HMODULE g_pluginModule;

// =============================================================================
// Vehicle classification helpers
// =============================================================================
namespace {

bool IsVehicleClassExcluded(int vehicleClass) {
    for (const int excluded : Config::ExcludedVehicleClasses) {
        if (excluded == vehicleClass) return true;
    }
    return false;
}

bool ComputeIsValidVehicle(Vehicle vehicle) {
    if (vehicle == 0 || !ENTITY::DOES_ENTITY_EXIST(vehicle)) return false;

    const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);

    if (VEHICLE::IS_THIS_MODEL_A_PLANE(model)   ||
        VEHICLE::IS_THIS_MODEL_A_HELI(model)    ||
        VEHICLE::IS_THIS_MODEL_A_BOAT(model)    ||
        VEHICLE::IS_THIS_MODEL_A_JETSKI(model)  ||
        VEHICLE::IS_THIS_MODEL_A_TRAIN(model)   ||
        VEHICLE::IS_THIS_MODEL_A_BICYCLE(model)) {
        return false;
    }

    if (!Config::AllowQuadbikes && VEHICLE::IS_THIS_MODEL_A_QUADBIKE(model))
        return false;

    if (IsVehicleClassExcluded(VEHICLE::GET_VEHICLE_CLASS(vehicle)))
        return false;

    return true;
}

// Model-hash cache so we only call the expensive model checks once per model.
bool IsValidVehicle(Vehicle vehicle) {
    if (vehicle == 0 || !ENTITY::DOES_ENTITY_EXIST(vehicle)) return false;

    static std::unordered_map<Hash, bool> s_classCache;
    const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
    const auto it    = s_classCache.find(model);
    if (it != s_classCache.end()) return it->second;

    const bool valid = ComputeIsValidVehicle(vehicle);
    if (valid)
        s_classCache.emplace(model, true);
    return valid;
}

bool IsPlayerDriving(Ped playerPed, Vehicle vehicle) {
    return VEHICLE::GET_PED_IN_VEHICLE_SEAT(vehicle, -1, 0) == playerPed;
}

// ── Calibration HUD helper ────────────────────────────────────────────────────
void DrawCalibrationHUD(CalibrationState state, float smoothedThrottle) {
    std::string calibMsg = "Calibration: ";
    switch (state) {
        case CalibrationState::Failed:
            calibMsg += "Failed. Check notification or Menu.";
            break;
        case CalibrationState::WaitingForEngineOff:
            calibMsg += "Turn off engine (press " +
                        std::string(1, static_cast<char>(Config::KeyEngine)) + ")";
            break;
        case CalibrationState::WaitingForEngineOn:
            calibMsg += "Turn ON engine (press " +
                        std::string(1, static_cast<char>(Config::KeyEngine)) + ") and idle";
            break;
        case CalibrationState::ScanningEngineOff:
            calibMsg += "Waiting for RPM to settle (engine off)...";
            break;
        case CalibrationState::ScanningEngineOn:
            calibMsg += "Sampling idle RPM...";
            break;
        case CalibrationState::WaitingForRev:
            calibMsg += "Rev the engine (Hold W)";
            break;
        case CalibrationState::ScanningRev:
            calibMsg += "Sampling rev RPM...";
            break;
        case CalibrationState::Done:
            calibMsg += "Success! Offsets saved.";
            break;
        default:
            calibMsg += "Scanning... (" +
                        std::to_string(VehicleData::GetCalibrationCandidateCount()) +
                        " candidates left)";
            break;
    }

    Renderer::DrawTextOverlay(calibMsg.c_str(), 0.5f, 0.10f, 0.60f);

    // Debug row: raw key state + smoothed throttle
    char throttleDbg[128]{};
    sprintf_s(throttleDbg,
              "[debug] raw W: %s | smoothed throttle: %.2f | calib state: %d",
              (GetAsyncKeyState(0x57) & 0x8000) ? "PRESSED" : "released",
              smoothedThrottle,
              static_cast<int>(state));
    Renderer::DrawTextOverlay(throttleDbg, 0.5f, 0.15f, 0.36f);
}

} // namespace

// =============================================================================
// ScriptMain — registered with ScriptHookV via scriptRegister()
// =============================================================================
void ScriptMain() {
    LOG_INFO(SCRIPT, "ScriptMain started, waiting for player ped...");

    // ── Phase 1: wait until the player ped is alive ───────────────────────────
    while (true) {
        scriptWait(1000);
        const Ped playerPed = PLAYER::PLAYER_PED_ID();
        if (ENTITY::DOES_ENTITY_EXIST(playerPed) && !PED::IS_PED_INJURED(playerPed))
            break;
    }
    LOG_INFO(SCRIPT, "Player ped ready. Running VehicleData::Initialize...");

    // ── Phase 2: initialize memory offsets ───────────────────────────────────
    if (!VehicleData::Initialize(g_pluginModule)) {
        const std::string buildVer = VehicleData::GetGameBuildVersion();
        const std::string reason   = VehicleData::GetLastFailureReason();

        LOG_FATAL(MEM, "VehicleData::Initialize failed! Build: %s | Reason: %s",
                  buildVer.empty() ? "unknown" : buildVer.c_str(),
                  reason.c_str());

        char msg[256]{};
        sprintf_s(msg, "Manual transmission disabled (build %s): %s",
                  buildVer.empty() ? "unknown" : buildVer.c_str(),
                  reason.c_str());
        Renderer::ShowNotification(msg);
        return;
    }
    LOG_INFO(SCRIPT, "VehicleData::Initialize OK. Reading config...");

    Config::ReadConfig(g_pluginModule);

    // Apply log verbosity from config
    if (Config::DebugOverlay)
        ModLogger::SetMinLevel(ModLogger::Level::DEBUG);
    else
        ModLogger::SetMinLevel(ModLogger::Level::INFO);

    LOG_INFO(SCRIPT, "Config loaded. Debug logging: %s",
             Config::DebugOverlay ? "VERBOSE" : "INFO");

    // ── Per-session state ─────────────────────────────────────────────────────
    bool     notificationShown    = false;
    Vehicle  activeVehicle        = 0;
    bool     activeLayoutValidated = false;
    bool     isEngineOn           = true;
    int      grindWarningTimer    = 0;
    int      manualGear           = 0;
    int      activeSignal         = 0;
    ULONGLONG s_vehicleEnterTime  = 0;

    // Calibration failure de-dupe
    static CalibrationState s_lastCalibState = CalibrationState::None;

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (true) {
        scriptWait(0);

        // Force-recalibrate requested from menu
        if (Config::ForceRecalibrate) {
            Config::ForceRecalibrate = false;
            LOG_INFO(CALIB, "ForceRecalibrate requested — resetting calibration state.");
            VehicleData::ResetCalibration();
            s_lastCalibState = CalibrationState::None;
        }

        Menu::Update();

        const Ped playerPed = PLAYER::PLAYER_PED_ID();

        // ── Not in a vehicle ──────────────────────────────────────────────────
        if (!PED::IS_PED_IN_ANY_VEHICLE(playerPed, FALSE)) {
            if (activeVehicle != 0) {
                LOG_INFO(SCRIPT, "Player exited vehicle %d. Resetting session state.", activeVehicle);
            }
            activeVehicle = 0;
            activeLayoutValidated = false;
            activeSignal = 0;
            InputHandler::ResetEdges();
            Menu::Draw();
            continue;
        }

        const Vehicle vehicle = PED::GET_VEHICLE_PED_IS_IN(playerPed, FALSE);

        // ── Not a valid manual-trans vehicle ─────────────────────────────────
        if (!IsValidVehicle(vehicle) || !IsPlayerDriving(playerPed, vehicle)) {
            activeVehicle = 0;
            activeLayoutValidated = false;
            activeSignal = 0;
            InputHandler::ResetEdges();
            Menu::Draw();
            continue;
        }

        // ── Show one-time "loaded" notification ───────────────────────────────
        if (!notificationShown && VehicleData::IsInitialized()) {
            notificationShown = true;
            const VehicleOffsets& offsets = VehicleData::GetResolvedOffsets();
            const std::string     buildVer = VehicleData::GetGameBuildVersion();

            char msg[256]{};
            sprintf_s(msg,
                      "Manual transmission: %s | build %s | G:%X N:%X R:%X C:%X",
                      VehicleData::GetOffsetSourceName(),
                      buildVer.empty() ? "unknown" : buildVer.c_str(),
                      offsets.Gear, offsets.NextGear, offsets.RPM, offsets.Clutch);
            Renderer::ShowNotification(msg);

            LOG_INFO(INIT, "Mod active — source=%s build=%s G=0x%X N=0x%X RPM=0x%X CLT=0x%X",
                     VehicleData::GetOffsetSourceName(),
                     buildVer.empty() ? "unknown" : buildVer.c_str(),
                     offsets.Gear, offsets.NextGear, offsets.RPM, offsets.Clutch);
        }

        const int maxGear = VEHICLE::_GET_VEHICLE_MAX_DRIVE_GEAR_COUNT(vehicle);
        VehicleData data(vehicle);
        const bool actualEngineOn = VEHICLE::GET_IS_VEHICLE_ENGINE_RUNNING(vehicle) != 0;

        // ── Vehicle change ────────────────────────────────────────────────────
        if (vehicle != activeVehicle) {
            LOG_INFO(SCRIPT, "Entered vehicle handle=%d maxGear=%d. Resetting subsystems.", vehicle, maxGear);

            activeVehicle = vehicle;
            activeLayoutValidated = true;

            if (Config::RequireColdStart) {
                VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
                isEngineOn = false;
                LOG_INFO(SCRIPT, "Cold start required — engine set OFF for vehicle %d.", vehicle);
            } else {
                isEngineOn = actualEngineOn;
            }

            GearLogic::Reset(0);
            PhysicsEngine::Reset();
            ParkingBrake::Reset();
            FuelSystem::Reset();
            TractionControl::Reset();
            TurboSystem::Reset();
            TurboSystem::InitializeForVehicle(vehicle);

            if (TelemetryLogger::IsLogging()) TelemetryLogger::StopSession();
            TelemetryLogger::StartSession(std::to_string(vehicle));

            s_vehicleEnterTime = GetTickCount64();
            activeSignal = 0;
        }

        // ── Skip frames while vehicle physics initializes ─────────────────────
        if (GetTickCount64() - s_vehicleEnterTime < 500) {
            Menu::Draw();
            continue;
        }

        InputHandler::Update();

        // ── Engine on/off key ─────────────────────────────────────────────────
        if (InputHandler::IsEngineJustPressed()) {
            isEngineOn = !isEngineOn;
            LOG_INFO(SCRIPT, "Engine key pressed — new state: %s", isEngineOn ? "ON" : "OFF");
            VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, isEngineOn ? TRUE : FALSE, TRUE, TRUE);
        } else if (!isEngineOn && actualEngineOn) {
            // Game turned engine back on (e.g. AI restart). Correct it.
            VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
            LOG_DEBUG(SCRIPT, "Engine state mismatch corrected: we=OFF game=ON → forcing OFF.");
        } else if (isEngineOn && !actualEngineOn) {
            // Engine died externally (fire / destroyed / fuel out).
            isEngineOn = false;
            LOG_WARN(SCRIPT, "Engine died externally (fire/destroyed/fuel). Reflecting state.");
        }

        // ── Turn signals ──────────────────────────────────────────────────────
        {
            constexpr int kIndicatorLeft  = 1;
            constexpr int kIndicatorRight = 0;

            if (InputHandler::IsSignalLeftJustPressed()) {
                activeSignal = (activeSignal == 1) ? 0 : 1;
                VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(vehicle, kIndicatorRight, FALSE);
                VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(vehicle, kIndicatorLeft, activeSignal == 1);
                LOG_DEBUG(SIGNAL, "Signal LEFT toggled — activeSignal=%d", activeSignal);
            } else if (InputHandler::IsSignalRightJustPressed()) {
                activeSignal = (activeSignal == 2) ? 0 : 2;
                VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(vehicle, kIndicatorLeft, FALSE);
                VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(vehicle, kIndicatorRight, activeSignal == 2);
                LOG_DEBUG(SIGNAL, "Signal RIGHT toggled — activeSignal=%d", activeSignal);
            }
        }

        // ── Calibration path ──────────────────────────────────────────────────
        if (!VehicleData::IsInitialized()) {
            const float smoothedThrottle = InputHandler::GetSmoothedThrottle();
            const bool  isRevving        = smoothedThrottle > 0.5f;

            VehicleData::UpdateCalibration(g_pluginModule, vehicle, isEngineOn, isRevving);

            const CalibrationState state = VehicleData::GetCalibrationState();

            // Log state transitions
            if (state != s_lastCalibState) {
                LOG_INFO(CALIB, "State transition: %d → %d | candidates=%zu",
                         static_cast<int>(s_lastCalibState),
                         static_cast<int>(state),
                         VehicleData::GetCalibrationCandidateCount());
                s_lastCalibState = state;
            }

            if (VehicleData::IsInitialized()) {
                const VehicleOffsets& offsets = VehicleData::GetResolvedOffsets();
                LOG_INFO(CALIB, "Calibration SUCCESS — RPM=0x%X CLT=0x%X G=0x%X N=0x%X TG=0x%X",
                         offsets.RPM, offsets.Clutch,
                         offsets.Gear, offsets.NextGear, offsets.TopGear);

                activeLayoutValidated = data.HasPlausibleLayout(maxGear > 0 ? maxGear : 6);
                Renderer::ShowNotification("Calibration complete! Manual transmission active.");
            } else {
                // Show failure notification once per failure event
                if (state == CalibrationState::Failed &&
                    s_lastCalibState != CalibrationState::Failed) {
                    const std::string reason = VehicleData::GetLastFailureReason();
                    LOG_ERROR(CALIB, "Calibration FAILED: %s", reason.c_str());
                    std::string failMsg = "~r~Calibration Failed:~w~\n" + reason;
                    Renderer::ShowNotification(failMsg.c_str());
                }

                DrawCalibrationHUD(state, smoothedThrottle);

                // Extended debug: show candidate count every second
                LOG_DEBUG_T(CALIB, 1000,
                            "Calibration in progress — state=%d candidates=%zu isRevving=%d throttle=%.2f",
                            static_cast<int>(state),
                            VehicleData::GetCalibrationCandidateCount(),
                            static_cast<int>(isRevving),
                            smoothedThrottle);
            }

            Menu::Draw();
            continue;
        }

        // ── Layout validation ─────────────────────────────────────────────────
        if (!activeLayoutValidated || !data.IsValid()) {
            LOG_WARN_T(MEM, 2000,
                       "Layout not validated or VehicleData invalid — skipping frame. "
                       "validated=%d dataValid=%d",
                       static_cast<int>(activeLayoutValidated),
                       static_cast<int>(data.IsValid()));
            Menu::Draw();
            continue;
        }

        // Continuous sanity check — calibration-derived offsets can drift.
        if (!data.HasPlausibleLayout(maxGear > 0 ? maxGear : 6)) {
            activeLayoutValidated = false;
            LOG_ERROR(MEM,
                      "Plausibility check FAILED mid-session! "
                      "gear=%u nextGear=%u rpm=%.4f clutch=%.4f maxGear=%d. "
                      "Disabling mod for this vehicle.",
                      static_cast<unsigned>(data.GetGear()),
                      static_cast<unsigned>(data.GetNextGear()),
                      data.GetRPM(), data.GetClutch(), maxGear);
            Renderer::ShowNotification(
                "Manual transmission: memory layout looks wrong, disabling for "
                "this vehicle. Try recalibrating.");
            Menu::Draw();
            continue;
        }

        // Deluxo hover-mode skip
        if (data.GetHoverTransformRatioLerp() > 0.0f) {
            LOG_DEBUG_T(SCRIPT, 3000, "Deluxo hover mode active — skipping manual trans logic.");
            Menu::Draw();
            continue;
        }

        // ── Per-frame values ──────────────────────────────────────────────────
        const float vehicleSpeed = ENTITY::GET_ENTITY_SPEED(vehicle);
        const float speedKmH     = vehicleSpeed * 3.6f;
        const float forwardSpeed = ENTITY::GET_ENTITY_SPEED_VECTOR(vehicle, TRUE).y;
        const float clutch       = InputHandler::GetSmoothedClutch();
        const float throttle     = InputHandler::GetSmoothedThrottle();
        const float rpm          = data.GetRPM();

        // Debug dump every 500 ms to avoid log spam
        LOG_DEBUG_T(INPUT, 500,
                    "throttle=%.3f brake=%.3f clutch=%.3f steer=%.3f "
                    "rpm=%.4f speed=%.1fkm/h gear=%d",
                    throttle,
                    InputHandler::GetSmoothedBrake(),
                    clutch,
                    InputHandler::GetSmoothedSteer(),
                    rpm, speedKmH, manualGear);

        // ── Clutch simulation ─────────────────────────────────────────────────
        float simulatedClutch = PhysicsEngine::UpdateClutch(clutch, throttle, rpm, isEngineOn);

        // ── Parking brake ─────────────────────────────────────────────────────
        const bool parkingBrakeOn = ParkingBrake::Update(
            vehicle, data, speedKmH, throttle, manualGear, isEngineOn);

        // ── TCS & ABS ─────────────────────────────────────────────────────────
        float tcsThrottle = throttle;
        float absBrake    = InputHandler::GetSmoothedBrake();
        TractionControl::Update(vehicle, data, speedKmH, rpm, manualGear,
                                tcsThrottle, absBrake);

        if (TractionControl::IsTCSActive())
            LOG_DEBUG_T(PHYSICS, 500, "TCS active — throttle limited to %.3f", tcsThrottle);
        if (TractionControl::IsABSActive())
            LOG_DEBUG_T(PHYSICS, 500, "ABS active — brake limited to %.3f", absBrake);

        // ── Turbo ─────────────────────────────────────────────────────────────
        float turboMultiplier = TurboSystem::Update(vehicle, data, rpm, tcsThrottle, isEngineOn);
        if (turboMultiplier > 1.05f) {
            VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(vehicle, turboMultiplier);
            LOG_DEBUG_T(TURBO, 1000, "Boost active — multiplier=%.3f pressure=%.3f",
                        turboMultiplier, TurboSystem::GetBoostPressure());
        }

        // ── Apply native controls ─────────────────────────────────────────────
        InputHandler::ApplyGameControls(manualGear, simulatedClutch, rpm, maxGear, forwardSpeed);
        if (tcsThrottle < throttle)
            PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 71, tcsThrottle);
        if (absBrake < InputHandler::GetSmoothedBrake()) {
            if (forwardSpeed > 0.1f)
                PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 72, absBrake);
            else
                PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 76, absBrake);
        }

        const bool wasEngineOn = isEngineOn;

        // ── Gear logic ────────────────────────────────────────────────────────
        manualGear = GearLogic::Update(
            vehicle, data, maxGear,
            InputHandler::IsShiftUpJustPressed(),
            InputHandler::IsShiftDownJustPressed(),
            simulatedClutch, throttle, speedKmH,
            isEngineOn, grindWarningTimer);

        if (wasEngineOn && !isEngineOn) {
            LOG_WARN(GEAR, "Engine stalled — gear=%d speed=%.1fkm/h clutch=%.3f",
                     manualGear, speedKmH, simulatedClutch);
            Renderer::ShowNotification("Engine Stalled! (Depress clutch or shift to Neutral N)");
        }

        // ── Post-gear physics ─────────────────────────────────────────────────
        const bool physicsStall = PhysicsEngine::UpdatePostGear(
            vehicle, data, manualGear, maxGear,
            simulatedClutch, throttle, speedKmH,
            isEngineOn, grindWarningTimer);

        if (physicsStall && isEngineOn) {
            isEngineOn = false;
            VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
            LOG_WARN(PHYSICS, "Physics stall — clutch bite-point stall triggered. "
                              "gear=%d speed=%.1fkm/h clutchHeat=%.3f",
                     manualGear, speedKmH, PhysicsEngine::GetClutchHeat());
            Renderer::ShowNotification("Engine Stalled! (Clutch bite-point)");
        }

        // ── Fuel system ───────────────────────────────────────────────────────
        const bool fuelStall = FuelSystem::Update(
            vehicle, data, throttle, rpm, isEngineOn, speedKmH);

        if (fuelStall && isEngineOn) {
            isEngineOn = false;
            VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
            LOG_ERROR(FUEL, "Fuel stall! Fuel=%.3f OilTemp=%.3f",
                      FuelSystem::GetFuelLevel(), FuelSystem::GetOilTemperature());
            Renderer::ShowNotification("~r~OUT OF FUEL! Engine died.");
        }

        // ── Apply memory writes ───────────────────────────────────────────────
        GearLogic::ApplyToMemory(vehicle, data, manualGear, simulatedClutch);
        LightsLogic::Update(vehicle, data, manualGear,
                            InputHandler::GetSmoothedBrake(), throttle);

        // ── HUD ───────────────────────────────────────────────────────────────
        Renderer::DrawGearHUD(manualGear, maxGear);

        if (grindWarningTimer > 0) {
            --grindWarningTimer;
            Renderer::DrawGrindWarning();
            LOG_WARN_T(GEAR, 2000, "Gear grind! gear=%d clutch=%.3f rpm=%.4f",
                       manualGear, simulatedClutch, rpm);
        }

        if (Config::DebugOverlay) {
            Renderer::DrawDebugOverlay(
                manualGear,
                static_cast<unsigned>(data.GetGear()),
                static_cast<unsigned>(data.GetNextGear()),
                rpm, data.GetClutch(),
                VehicleData::GetOffsetSourceName());
        }

        if (Config::OverlayBars) {
            Renderer::DrawPedalsOverlay(rpm, simulatedClutch, throttle,
                                        InputHandler::GetSmoothedBrake());
            Renderer::DrawSimulationOverlay(
                FuelSystem::GetFuelLevel(), FuelSystem::GetOilTemperature(),
                PhysicsEngine::GetGearboxHealth(), PhysicsEngine::GetClutchHeat(),
                ParkingBrake::IsEngaged(), PhysicsEngine::AreRearWheelsLocked(),
                PhysicsEngine::GetEngineBrakeForce());

            Renderer::DrawTextOverlay(
                (std::string("TCS: ") +
                 (TractionControl::IsTCSActive() ? "~y~ACTIVE" : "~g~OK") +
                 " | ABS: " +
                 (TractionControl::IsABSActive() ? "~y~ACTIVE" : "~g~OK") +
                 (TurboSystem::HasTurbo()
                      ? (" | BOOST: " +
                         std::to_string(TurboSystem::GetBoostPressure()).substr(0, 4))
                      : ""))
                    .c_str(),
                Config::OverlayPosX,
                Config::OverlayPosY + Config::OverlayBarHeight * 5.0f, 0.35f);
        }

        // ── Telemetry ─────────────────────────────────────────────────────────
        TelemetryLogger::LogFrame(
            vehicle, data, speedKmH, rpm, tcsThrottle, absBrake,
            simulatedClutch, manualGear, InputHandler::GetSmoothedSteer(),
            TractionControl::IsTCSActive()  ? 1.0f : 0.0f,
            TractionControl::IsABSActive()  ? 1.0f : 0.0f,
            TurboSystem::GetBoostPressure(), FuelSystem::GetOilTemperature());

        Menu::Draw();
    } // while (true)
}
