// =============================================================================
// VehicleSessionController.cpp — Vehicle change detection and sub-system reset.
// Extracted from MainScript.cpp baris 293–361.
// =============================================================================
#include "VehicleSessionController.h"

#include "../../Core/Config.h"
#include "../../Core/ModLogger.h"
#include "../../Core/Renderer.h"
#include "../../Vehicle/Brakes/BrakeSystem.h"
#include "../../Vehicle/Brakes/ParkingBrake.h"
#include "../../Vehicle/Clutch/ClutchSystem.h"
#include "../../Vehicle/Engine/EngineModel.h"
#include "../../Vehicle/Engine/FuelSystem.h"
#include "../../Vehicle/Engine/LaunchControl.h"
#include "../../Vehicle/Engine/PedalModel.h"
#include "../../Vehicle/Engine/TractionControl.h"
#include "../../Vehicle/Engine/TurboSystem.h"
#include "../../Vehicle/Gears/AutomaticGearbox.h"
#include "../../Vehicle/Gears/GearboxSystem.h"
#include "../../Vehicle/Gears/GearLogic.h"
#include "../../Vehicle/Maintenance/MaintenanceSystem.h"
#include "../../Vehicle/Maintenance/RefuelInteraction.h"
#include "../../Vehicle/Maintenance/ServiceInteraction.h"
#include "../../Vehicle/Maintenance/WorkshopIntegration.h"
#include "../../Vehicle/TelemetryLogger.h"
#include "../../Vehicle/VehicleUpgrades.h"
#include "../../../sdk/inc/natives.h"

#include <algorithm>

void VehicleSessionController::Reset() {
    if (m_activeVehicle && ENTITY::DOES_ENTITY_EXIST(m_activeVehicle)) {
        VehicleData previousData(m_activeVehicle);
        previousData.SetClutch(1.0f);
        VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(m_activeVehicle, 1.0f);
    }
    m_activeVehicle = 0;
    m_enterTick     = 0;
    m_justChanged   = false;
    // m_notifyShown is NOT reset here — it persists across the session.
    WorkshopIntegration::Reset();
}

bool VehicleSessionController::CheckAndUpdate(Vehicle current, int maxGear) {
    m_justChanged = false;

    if (current == m_activeVehicle)
        return false;

    // Clean up previous vehicle
    if (m_activeVehicle && ENTITY::DOES_ENTITY_EXIST(m_activeVehicle)) {
        VehicleData previousData(m_activeVehicle);
        previousData.SetClutch(1.0f);
        VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(m_activeVehicle, 1.0f);
    }

    LOG_INFO(Script, "Entered vehicle handle=%d maxGear=%d", current, maxGear);
    m_activeVehicle = current;
    m_justChanged   = true;
    m_enterTick     = GetTickCount64();

    // Reset all sub-systems for the new vehicle
    ResetSubsystems(current);

    if (TelemetryLogger::IsLogging()) {
        TelemetryLogger::StopSession();
        LOG_INFO(Script, "Telemetry session stopped to prevent disk bloat.");
    }

    return true;
}

bool VehicleSessionController::IsInGracePeriod() const {
    return GetTickCount64() - m_enterTick < 500;
}

void VehicleSessionController::ResetSubsystems(Vehicle veh) {
    GearLogic::Reset(0);
    AutomaticGearbox::Reset();
    GearboxSystem::Reset();
    ClutchSystem::Reset();
    EngineModel::Reset();
    VehicleUpgrades::Reset();
    VehicleUpgrades::Initialize(veh);
    LaunchControl::Reset();
    PedalModel::Reset();
    BrakeSystem::Reset();
    ParkingBrake::Reset();
    FuelSystem::SelectVehicle(veh);
    RefuelInteraction::TrackVehicle(veh);
    MaintenanceSystem::SelectVehicle(veh);
    ServiceInteraction::TrackVehicle(veh);
    WorkshopIntegration::Reset();
    TractionControl::Reset();
    TurboSystem::Reset();
    TurboSystem::InitializeForVehicle(veh);
    VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(veh, 1.0f);

    const auto profile = VehicleProfile::Detect(veh);
    LOG_INFO(Script,
             "Vehicle profile=%s engineMod=%d/%d transmissionMod=%d/%d "
             "race=%d quick=%d power=%d",
             VehicleProfile::GetName(profile),
             VehicleUpgrades::GetState().engineLevel,
             VehicleUpgrades::GetState().engineMaxLevel,
             VehicleUpgrades::GetState().transmissionLevel,
             VehicleUpgrades::GetState().transmissionMaxLevel,
             VehicleUpgrades::GetState().raceTransmission ? 1 : 0,
             VehicleUpgrades::GetState().quickshifter ? 1 : 0,
             VehicleUpgrades::GetState().powershifter ? 1 : 0);
}
