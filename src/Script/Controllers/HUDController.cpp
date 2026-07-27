// =============================================================================
// HUDController.cpp — All HUD rendering extracted from MainScript.cpp 974–1074.
// =============================================================================
#include "HUDController.h"
#include "DriveAssistController.h"

#include "../../Core/Config.h"
#include "../../Core/InputHandler.h"
#include "../../Core/Menu.h"
#include "../../Core/Renderer.h"
#include "../../Vehicle/Brakes/BrakeSystem.h"
#include "../../Vehicle/Brakes/ParkingBrake.h"
#include "../../Vehicle/Clutch/ClutchSystem.h"
#include "../../Vehicle/Engine/EngineModel.h"
#include "../../Vehicle/Engine/FuelSystem.h"
#include "../../Vehicle/Engine/PedalModel.h"
#include "../../Vehicle/Engine/TurboSystem.h"
#include "../../Vehicle/Gearbox/Automatic/AutomaticGearbox.h"
#include "../../Vehicle/Gearbox/Core/GearboxSystem.h"
#include "../../Vehicle/Maintenance/MaintenanceSystem.h"
#include "../../../sdk/inc/natives.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace HUDController {

void Update(Vehicle veh, VehicleData& data,
            VehicleProfile::Drivetrain profile,
            int manualGear, int maxGear, int transmissionMode,
            float simulatedClutch, float throttle, float brake,
            float speedKmH, float rpm,
            bool isEngineOn, bool engineStarting,
            bool automaticMode, int activeSignal,
            int grindWarningTimer,
            const DriveAssistController& assist) {

    // ── Gear HUD ──────────────────────────────────────────────────────────
    if (Config::GearHudEnabled && !Config::SpeedometerEnabled &&
        !Menu::IsOpen()) {
        Renderer::DrawGearHUD(
            manualGear, maxGear, activeSignal, isEngineOn, engineStarting,
            transmissionMode,
            automaticMode ? AutomaticGearbox::GetSelectorName() : nullptr);
    }

    // ── Speedometer ───────────────────────────────────────────────────────
    if (Config::SpeedometerEnabled) {
        const auto& engineState = EngineModel::GetState();
        Renderer::SpeedometerData cluster{};
        cluster.speedKmH       = speedKmH;
        cluster.normalizedRPM  = data.GetRPM();
        cluster.physicalRPM    = engineState.estimatedEngineRPM;
        cluster.redlineRPM     = engineState.estimatedRedlineRPM;
        cluster.fuel           = FuelSystem::GetFuelLevel();
        cluster.oilTemperature = FuelSystem::GetOilTemperature();
        cluster.oilLife        = MaintenanceSystem::GetState().oilLife;
        cluster.engineHealth   =
            std::clamp(VEHICLE::GET_VEHICLE_ENGINE_HEALTH(veh) / 1000.0f,
                       0.0f, 1.0f);
        cluster.gearboxHealth  = GearboxSystem::GetHealth();
        cluster.clutchHeat     = ClutchSystem::GetHeat();
        cluster.boost          = TurboSystem::GetBoostPressure();
        cluster.odometerKm     = MaintenanceSystem::GetState().odometerKm;
        cluster.throttle       = throttle;
        cluster.brake          = brake;
        cluster.maximumSpeedKmH =
            VEHICLE::GET_VEHICLE_ESTIMATED_MAX_SPEED(veh) * 3.6f;
        cluster.gear           = manualGear;
        cluster.maxGear        = maxGear;
        cluster.transmissionMode = transmissionMode;
        cluster.automaticSelector =
            automaticMode ? AutomaticGearbox::GetSelectorName() : nullptr;
        const Hash model = ENTITY::GET_ENTITY_MODEL(veh);
        cluster.motorcycle =
            VEHICLE::IS_THIS_MODEL_A_BIKE(model) ||
            VEHICLE::IS_THIS_MODEL_A_QUADBIKE(model);
        cluster.electric =
            profile == VehicleProfile::Drivetrain::Electric;
        cluster.engineOn       = isEngineOn;
        cluster.engineStarting = engineStarting;
        cluster.parkingBrake   = ParkingBrake::IsEngaged();
        cluster.tcsEnabled     = Config::TcsEnabled;
        cluster.tcsActive      = assist.GetState().tcsActive;
        cluster.absEnabled     = Config::AbsEnabled;
        cluster.absActive      = assist.GetState().absActive;
        cluster.escEnabled     = Config::EscEnabled;
        cluster.escActive      = assist.GetState().escActive;
        cluster.launchEnabled  = Config::LaunchControl;
        cluster.rollWarning    = assist.GetState().rollWarning;
        cluster.launchControl  = assist.GetState().lcArmed;
        cluster.burnout        = engineState.burnoutActive;
        cluster.vehicleClass   = VEHICLE::GET_VEHICLE_CLASS(veh);
        cluster.modelHash      = static_cast<std::uint32_t>(model);
        Renderer::DrawSpeedometer(cluster);
    }

    // ── Grind warning ─────────────────────────────────────────────────────
    if (grindWarningTimer > 0 && !Menu::IsOpen()) {
        Renderer::DrawGrindWarning();
    }

    // ── Debug overlay ─────────────────────────────────────────────────────
    if (Config::DebugOverlay) {
        Renderer::DrawDebugOverlay(
            manualGear, static_cast<unsigned>(data.GetGear()),
            static_cast<unsigned>(data.GetNextGear()), rpm, data.GetClutch(),
            VehicleData::GetOffsetSourceName());
    }

    // ── Pedal + simulation overlay bars ───────────────────────────────────
    if (Config::OverlayBars && !Menu::IsOpen()) {
        Renderer::DrawPedalsOverlay(rpm,
                                    automaticMode ? -1.0f : simulatedClutch,
                                    throttle,
                                    InputHandler::GetSmoothedBrake());
        Renderer::DrawSimulationOverlay(
            FuelSystem::GetFuelLevel(), FuelSystem::GetOilTemperature(),
            MaintenanceSystem::GetState().oilLife,
            GearboxSystem::GetHealth(), ClutchSystem::GetHeat(),
            ParkingBrake::IsEngaged(), assist.GetState().absActive,
            EngineModel::GetEngineBrake());

        // ── Warning labels ────────────────────────────────────────────────
        std::string warnings;
        if (assist.GetState().tcsActive) warnings += "~y~TCS ";
        if (assist.GetState().absActive) warnings += "~y~ABS ";
        if (assist.GetState().escActive) warnings += "~y~ESC ";
        if (assist.GetState().lcArmed) warnings += "~b~LC ";
        if (assist.GetState().rollWarning) warnings += "~r~ROLLOVER ";
        if (PedalModel::IsBrakeOverrideActive()) warnings += "~r~BTO ";
        if (ClutchSystem::IsDumpActive()) warnings += "~r~CLUTCH DUMP ";
        if (GearboxSystem::GetState().moneyShift) warnings += "~r~OVERREV ";
        if (BrakeSystem::GetFadeLevel() > 0.01f) warnings += "~r~BRAKE FADE ";
        if (GearboxSystem::GetState().clashActive) warnings += "~r~GEAR CLASH ";
        if (TurboSystem::HasTurbo() &&
            TurboSystem::GetBoostPressure() > 0.05f)
            warnings += "~b~BOOST ";
        if (!warnings.empty()) {
            const float warningX =
                std::clamp(Config::GearHudPosX, 0.10f, 0.90f);
            const float warningY =
                std::clamp(Config::GearHudPosY +
                               0.095f * Config::GearHudScale,
                           0.14f, 0.78f);
            Renderer::DrawTextOverlay(
                warnings.c_str(), warningX, warningY, 0.31f,
                255, 255, 255, 255, 0, true, true);
        }
    }
}

} // namespace HUDController
