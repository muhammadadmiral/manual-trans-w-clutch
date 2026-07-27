// =============================================================================
// DiagnosticsController.cpp — Structured per-second status logging.
// Extracted from MainScript.cpp baris 884–972. Enhanced with fault registry.
// =============================================================================
#include "DiagnosticsController.h"

#include "../../Core/Config.h"
#include "../../Core/ModLogger.h"
#include "../../Memory/GearboxPatches.h"
#include "../../Vehicle/Brakes/BrakeSystem.h"
#include "../../Vehicle/Clutch/ClutchSystem.h"
#include "../../Vehicle/Engine/EngineModel.h"
#include "../../Vehicle/Engine/TractionControl.h"
#include "../../Vehicle/Gears/AutomaticGearbox.h"
#include "../../Vehicle/Gears/GearboxSystem.h"

#include <algorithm>

namespace DiagnosticsController {

static DWORD s_lastStatusLog = 0;
static std::vector<std::string> s_faults;

void Reset() {
    s_lastStatusLog = 0;
    s_faults.clear();
}

void RecordFault(const char* faultCode) {
    // Deduplicate — hanya simpan unique faults.
    const std::string code(faultCode);
    for (const auto& existing : s_faults)
        if (existing == code)
            return;
    s_faults.push_back(code);
    LOG_WARN(Diag, "Fault recorded: %s (total=%zu)", faultCode, s_faults.size());
}

const std::vector<std::string>& GetFaults() {
    return s_faults;
}

void Update(Vehicle veh, VehicleData& data,
            VehicleProfile::Drivetrain profile,
            int manualGear, int transmissionMode,
            float driveThrottle, float absBrake, float simulatedClutch,
            float speedKmH, float forwardSpeed,
            bool isEngineOn, bool engineStarting, bool automaticMode) {

    if (GetTickCount() - s_lastStatusLog < 1000)
        return;

    const auto& engineState = EngineModel::GetState();
    const auto& clutchState = ClutchSystem::GetState();
    const auto& gearboxState = GearboxSystem::GetState();
    const auto& tcsState = TractionControl::GetState();
    const auto& absState = BrakeSystem::GetState();
    const auto& autoState = AutomaticGearbox::GetState();

    LOG_INFO(Gear,
             "STATUS: Mode=%d Profile=%s Selector=%s Gear=%d Mem=%u Next=%u "
             "Speed=%.1f Signed=%.2f RPM=%.3f Throttle=%.3f Brake=%.3f "
             "PedalClutch=%.3f MemClutch=%.3f Engine=%d Start=%d Patch=%d",
             transmissionMode, VehicleProfile::GetName(profile),
             automaticMode ? AutomaticGearbox::GetSelectorName() : "M",
             manualGear, static_cast<unsigned>(data.GetGear()),
             static_cast<unsigned>(data.GetNextGear()), speedKmH,
             forwardSpeed, data.GetRPM(), driveThrottle, absBrake,
             simulatedClutch, data.GetClutch(), isEngineOn ? 1 : 0,
             engineStarting ? 1 : 0,
             GearboxPatches::IsApplied() ? 1 : 0);
    LOG_INFO(Physics,
             "ENGINE: Owned=%d CtrlRPM=%.3f Target=%.3f WheelRPM=%.3f "
             "Physical=%.0f Idle=%.0f Redline=%.0f Load=%.3f "
             "Reserve=%.3f Lug=%.3f Stall=%.3f LowRec=%.3f "
             "GearLimit=%.1fkm/h Driven=%.1fkm/h WheelData=%d "
             "Burnout=%d Adaptive=%d",
             engineState.rpmOwned ? 1 : 0, engineState.controlledRPM,
             engineState.connectedRPMTarget, engineState.wheelRPM,
             engineState.estimatedEngineRPM,
             engineState.estimatedIdlePhysicalRPM,
             engineState.estimatedRedlineRPM, engineState.load,
             engineState.torqueReserve, engineState.lugSeverity,
             engineState.stallProgress, engineState.lowRpmRecovery,
             engineState.gearLimitSpeedMps * 3.6f,
             engineState.drivenWheelSpeedMps * 3.6f,
             engineState.wheelTelemetryValid ? 1 : 0,
             engineState.burnoutActive ? 1 : 0,
             engineState.adaptiveGearing ? 1 : 0);
    LOG_INFO(Physics,
             "IDLE_DRIVE: Creep=%.3f HillRollback=%d Power=%.3f "
             "Profile=%s",
             engineState.creepThrottle,
             engineState.hillRollback ? 1 : 0,
             engineState.driveTorqueFactor,
             VehicleProfile::GetName(profile));
    LOG_INFO(Physics,
             "CLUTCH_GEARBOX: Engage=%.3f Demand=%.3f Cap=%.3f "
             "OSlip=%.3f Heat=%.3f Judder=%.3f Clash=%.3f Shock=%.3f "
             "SyncWear=%.3f ResistMs=%u Reject=%d Money=%d WheelLock=%.3f",
             clutchState.engagement, clutchState.torqueDemand,
             clutchState.torqueCapacity, clutchState.overloadSlip,
             clutchState.heat, clutchState.judder,
             gearboxState.clashSeverity, gearboxState.shockRemaining,
             gearboxState.selectedSynchroWear,
             static_cast<unsigned>(gearboxState.resistanceDelayMs),
             gearboxState.shiftRejected ? 1 : 0,
             gearboxState.moneyShift ? 1 : 0,
             GearboxSystem::GetWheelLockBrake());
    LOG_INFO(Physics,
             "ASSISTS: TCSEn=%d TCSReady=%d TCSWheels=%d Slip=%.3f "
             "Cut=%.3f Active=%d ABSEn=%d ABSReady=%d ABSWheels=%d "
             "ABSSlip=%.3f ABSLevel=%.3f Active=%d",
             tcsState.enabled ? 1 : 0, tcsState.wheelDataValid ? 1 : 0,
             tcsState.validWheelCount, tcsState.slipRatio,
             tcsState.cutLevel, TractionControl::IsTCSActive() ? 1 : 0,
             absState.absEnabled ? 1 : 0,
             absState.wheelDataValid ? 1 : 0, absState.validWheelCount,
             absState.wheelSlip, absState.absLevel,
             BrakeSystem::IsABSActive() ? 1 : 0);
    if (automaticMode) {
        LOG_INFO(Gear,
                 "AUTO: Phase=%s Current=%d Pending=%d Hydraulic=%.3f "
                 "Native=%.3f "
                 "DecisionRPM=%.3f TCC=%d ATF=%.3f Limp=%d KDWait=%d "
                 "NDrop=%d Boost=%.2f TM=%.3f IgnCut=%d",
                 AutomaticGearbox::GetShiftPhaseName(),
                 autoState.currentGear, autoState.pendingGear,
                 autoState.hydraulicCoupling, autoState.coupling,
                 autoState.decisionRPM,
                 autoState.tccLocked ? 1 : 0, autoState.fluidTemperature,
                 autoState.limpMode ? 1 : 0,
                 autoState.kickdownPending ? 1 : 0,
                 autoState.neutralDrop ? 1 : 0, autoState.brakeBoostTime,
                 autoState.torqueManagement,
                 autoState.ignitionCut ? 1 : 0);
    }

    // v1.0: Auto-record faults from system states
    if (gearboxState.moneyShift)
        RecordFault("money_shift");
    if (clutchState.heat > 0.85f)
        RecordFault("clutch_overheat");
    if (gearboxState.health < 0.3f)
        RecordFault("gearbox_critical");
    if (engineState.environmentStall)
        RecordFault("environment_stall");

    s_lastStatusLog = GetTickCount();
}

} // namespace DiagnosticsController
