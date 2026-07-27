// =============================================================================
// TransmissionController.cpp — Core per-frame physics loop.
// Extracted from MainScript.cpp baris 579–873. This is the largest controller:
// mode switching, clutch/gear calc, pedal model, TCS, ABS, turbo, fuel,
// memory writes, and stall detection.
// =============================================================================
#include "TransmissionController.h"

#include "../../Core/Config.h"
#include "../../Core/InputHandler.h"
#include "../../Core/ModLogger.h"
#include "../../Core/Renderer.h"
#include "../../Memory/GearboxPatches.h"
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
#include "../../Vehicle/LightsLogic.h"
#include "../../Vehicle/Maintenance/MaintenanceSystem.h"
#include "../../Vehicle/VehicleUpgrades.h"
#include "../../../sdk/inc/natives.h"

#include <algorithm>
#include <cmath>
#include <string>

void TransmissionController::Reset() {
    m_manualGear           = 0;
    m_activeMode           = -1;
    m_automaticClutchUntil = 0;
    m_patchFailureShown    = false;
    m_firstFrameTrace      = true;
    m_wasFirstFrame        = false;
    m_grindWarningTimer    = 0;
    m_stallPending         = false;
    m_simulatedClutch      = 0.0f;
    m_driveThrottle        = 0.0f;
    m_absBrake             = 0.0f;
    m_powerMultiplier      = 1.0f;
}

bool TransmissionController::ConsumedStallEvent() {
    const bool v = m_stallPending;
    m_stallPending = false;
    return v;
}

void TransmissionController::Update(
    Vehicle veh, VehicleData& data,
    VehicleProfile::Drivetrain profile,
    bool isEngineOn, bool workshopOpen,
    float vehicleSpeed, float forwardSpeed, int maxGear) {

    m_wasFirstFrame = false;

    // ── Mode resolution ───────────────────────────────────────────────────
    const int requestedMode = std::clamp(Config::TransmissionMode, 0, 2);
    const bool electricVehicle =
        profile == VehicleProfile::Drivetrain::Electric;
    const bool scooterVehicle =
        profile == VehicleProfile::Drivetrain::ScooterCVT;
    const bool utilitySingleSpeed =
        profile == VehicleProfile::Drivetrain::UtilitySingleSpeed;
    const int transmissionMode =
        VehicleProfile::ForcesAutomatic(profile) ? 1 : requestedMode;
    const bool nativePatchReady =
        GearboxPatches::SetActive(transmissionMode != 0);
    if (transmissionMode != 0 && !nativePatchReady && !m_patchFailureShown) {
        m_patchFailureShown = true;
        const std::string reason = GearboxPatches::GetFailureReason();
        Renderer::ShowNotification(
            ("~r~Native gearbox patch gagal:~w~ " +
             (reason.empty() ? std::string("signature Enhanced tidak cocok")
                             : reason))
                .c_str());
    }

    // ── Mode change ───────────────────────────────────────────────────────
    if (transmissionMode != m_activeMode) {
        if (m_activeMode == 1)
            VEHICLE::SET_VEHICLE_HANDBRAKE(veh, FALSE);
        GearLogic::Reset(0);
        AutomaticGearbox::Reset(
            scooterVehicle ? AutomaticGearbox::Selector::Drive
                           : AutomaticGearbox::Selector::Park);
        GearboxSystem::Reset();
        ClutchSystem::Reset();
        EngineModel::Reset();
        PedalModel::Reset();
        m_manualGear = 0;
        m_automaticClutchUntil = 0;
        data.SetClutch(1.0f);
        VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(veh, 1.0f);
        m_activeMode = transmissionMode;
        m_firstFrameTrace = true;

        if (electricVehicle && requestedMode == 2) {
            Renderer::ShowNotification(
                "~y~EV detected:~w~ manual locked, automatic active");
        } else if (scooterVehicle && requestedMode != 1) {
            Renderer::ShowNotification(
                "~y~Scooter CVT:~w~ gas/rem only, clutch dan manual nonaktif");
        } else if (utilitySingleSpeed && requestedMode != 1) {
            Renderer::ShowNotification(
                "~y~Utility drive:~w~ single-speed automatic aktif");
        } else {
            Renderer::ShowNotification(
                transmissionMode == 0
                    ? "Transmission mod: ~c~OFF"
                    : (transmissionMode == 1
                           ? "Transmission: ~b~AUTOMATIC P-R-N-D-S-L2-L1"
                           : "Transmission: ~g~MANUAL SEQUENTIAL"));
        }
        LOG_INFO(Gear, "Transmission mode=%d requested=%d profile=%s",
                 transmissionMode, requestedMode,
                 VehicleProfile::GetName(profile));
    }

    if (transmissionMode == 0) {
        GearboxPatches::SetActive(false);
        data.SetClutch(1.0f);
        VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(veh, 1.0f);
        return;
    }

    // ── Per-frame values ──────────────────────────────────────────────────
    const float speedKmH = vehicleSpeed * 3.6f;
    const bool automaticMode = transmissionMode == 1;
    const bool motorcycleAutoClutch =
        transmissionMode == 2 &&
        profile == VehicleProfile::Drivetrain::MotorcycleSequential;
    const bool shiftUpPressed = InputHandler::IsShiftUpJustPressed();
    const bool shiftDownPressed = InputHandler::IsShiftDownJustPressed();
    const bool nativeQuickshift =
        VehicleUpgrades::GetState().quickshifter && shiftUpPressed &&
        m_manualGear > 0;
    if (motorcycleAutoClutch && (shiftUpPressed || shiftDownPressed) &&
        !nativeQuickshift) {
        const ULONGLONG clutchWindow =
            VehicleUpgrades::GetState().transmissionStage > 0.0f ? 110 : 170;
        m_automaticClutchUntil = GetTickCount64() + clutchWindow;
    }
    const bool automaticClutchOpen =
        motorcycleAutoClutch && GetTickCount64() < m_automaticClutchUntil;
    const float clutch =
        automaticClutchOpen
            ? 1.0f
            : (!automaticMode && InputHandler::IsClutchDown()
                   ? 1.0f
                   : (!automaticMode ? InputHandler::GetSmoothedClutch()
                                     : 0.0f));
    float rawThrottle =
        workshopOpen ? 0.0f : InputHandler::GetSmoothedThrottle();
    const float rawBrake =
        workshopOpen ? 1.0f : InputHandler::GetSmoothedBrake();
    const float rpm = data.GetRPM();
    VehicleUpgrades::Refresh(veh);
    const bool traceFrame = m_firstFrameTrace;
    if (traceFrame) {
        LOG_INFO(Script, "TRACE v1 stage=frame-begin gear=%d rpm=%.3f",
                 m_manualGear, rpm);
        m_wasFirstFrame = true;
    }

    if (automaticMode &&
        AutomaticGearbox::GetSelector() ==
            AutomaticGearbox::Selector::Drive &&
        InputHandler::IsKeyboardThrottle()) {
        rawThrottle =
            std::min(rawThrottle,
                     std::clamp(Config::AutomaticDKeyboardThrottle,
                                0.30f, 1.00f));
    }

    // ── Subsystem updates ─────────────────────────────────────────────────
    m_simulatedClutch =
        automaticMode
            ? AutomaticGearbox::GetClutchDisengagement()
            : ClutchSystem::UpdatePedal(clutch, rawThrottle, rpm, m_manualGear,
                                        maxGear, isEngineOn);
    PedalModel::Update(rawThrottle, rawBrake, m_simulatedClutch, m_manualGear,
                       forwardSpeed, automaticMode, isEngineOn);
    float throttle = PedalModel::GetThrottle();
    float brake = PedalModel::GetBrake();

    if (automaticMode) {
        if (!scooterVehicle) {
            AutomaticGearbox::UpdateSelector(
                veh, shiftUpPressed, shiftDownPressed, brake, forwardSpeed,
                rpm);
        }
        m_manualGear = AutomaticGearbox::Update(
            veh, data, maxGear, throttle, brake, forwardSpeed, isEngineOn);
        m_simulatedClutch = AutomaticGearbox::GetClutchDisengagement();
    } else {
        m_manualGear = GearLogic::Update(
            veh, data, maxGear, shiftUpPressed, shiftDownPressed,
            m_simulatedClutch, throttle,
            speedKmH, isEngineOn, m_grindWarningTimer);
    }
    if (traceFrame)
        LOG_INFO(Script, "TRACE v1 stage=shift-model gear=%d clutch=%.3f",
                 m_manualGear, m_simulatedClutch);

    const bool parkingBrakeOn = ParkingBrake::Update(
        veh, data, speedKmH, throttle, m_manualGear, isEngineOn);
    if (automaticMode &&
        (ParkingBrake::WasJustPressed() || parkingBrakeOn) &&
        speedKmH > 5.0f) {
        AutomaticGearbox::ForceNeutral();
        m_manualGear = 0;
        m_simulatedClutch = 1.0f;
    }

    GearboxSystem::Update(veh, data, m_manualGear, maxGear, m_simulatedClutch,
                          throttle, isEngineOn);

    float tcsThrottle = throttle;
    m_absBrake = brake;
    TractionControl::Update(veh, data, forwardSpeed, m_manualGear,
                            m_simulatedClutch, tcsThrottle);
    m_absBrake = BrakeSystem::UpdateABS(
        veh, data, m_absBrake, forwardSpeed, m_manualGear < 0);
    m_absBrake =
        std::max(m_absBrake, GearboxSystem::GetWheelLockBrake());
    if (Config::FuelCutoffEngineBrake && m_manualGear > 0 &&
        m_simulatedClutch < 0.20f && throttle < 0.01f && rpm > 0.30f) {
        m_absBrake = std::max(
            m_absBrake,
            std::clamp(EngineModel::GetEngineBrake() * 0.18f, 0.0f, 0.18f));
    }
    if (automaticMode)
        AutomaticGearbox::SetTorqueManagement(
            TractionControl::GetState().cutLevel);
    if (traceFrame)
        LOG_INFO(Script, "TRACE v1 stage=assists throttle=%.3f brake=%.3f",
                 tcsThrottle, m_absBrake);

    if (TractionControl::IsTCSActive())
        LOG_VERBOSE(Physics, "TCS active — throttle limited to %.3f", tcsThrottle);
    if (BrakeSystem::IsABSActive())
        LOG_VERBOSE(Physics, "ABS active — brake limited to %.3f", m_absBrake);

    const float turboMul =
        TurboSystem::Update(veh, data, rpm, throttle, isEngineOn);
    if (turboMul > 1.05f) {
        LOG_DEBUG_T(Turbo, 1000,
                    "Boost telemetry: native power, mul=%.3f press=%.3f",
                    turboMul,
                    TurboSystem::GetBoostPressure());
    }

    m_driveThrottle =
        automaticMode && AutomaticGearbox::IsSport()
            ? std::pow(std::clamp(tcsThrottle, 0.0f, 1.0f),
                       1.0f - std::clamp(Config::AutomaticSTorqueBoost,
                                         0.0f, 0.50f))
            : tcsThrottle;
    const float clutchEngagement =
        automaticMode ? AutomaticGearbox::GetCoupling()
                      : ClutchSystem::GetEngagement();
    const bool gentleClutchRelease =
        automaticMode || !ClutchSystem::IsDumpActive();
    const float idleDrive =
        parkingBrakeOn
            ? 0.0f
            : EngineModel::PrepareIdleDrive(
                  veh, data, m_manualGear, maxGear, clutchEngagement,
                  m_driveThrottle, m_absBrake, forwardSpeed, isEngineOn,
                  automaticMode, gentleClutchRelease);
    const float controlThrottle =
        std::max(m_driveThrottle, idleDrive);
    InputHandler::ApplyGameControls(veh, m_manualGear, m_simulatedClutch,
                                    controlThrottle, m_absBrake, maxGear,
                                    forwardSpeed);
    if (traceFrame)
        LOG_INFO(Script, "TRACE v1 stage=controls-applied");

    // ── Memory writes ─────────────────────────────────────────────────────
    if (automaticMode) {
        AutomaticGearbox::ApplyToMemory(veh, data, m_manualGear,
                                        controlThrottle);
    } else {
        GearLogic::ApplyToMemory(veh, data, m_manualGear, maxGear,
                                 m_simulatedClutch, throttle, speedKmH);
        ClutchSystem::ApplyToVehicle(data, m_manualGear, forwardSpeed);
    }

    // ── Engine model ──────────────────────────────────────────────────────
    const bool engineStall = EngineModel::Update(
        veh, data, m_manualGear, maxGear, m_simulatedClutch,
        clutchEngagement, m_driveThrottle, m_absBrake, forwardSpeed, isEngineOn,
        automaticMode);
    if (traceFrame)
        LOG_INFO(Script,
                 "TRACE v1 stage=engine-model rpm=%.3f power=%.3f stall=%d",
                 data.GetRPM(), EngineModel::GetDriveTorqueFactor(),
                 engineStall ? 1 : 0);

    const float sportTorque =
        automaticMode && AutomaticGearbox::IsSport()
            ? 1.0f + std::clamp(Config::AutomaticSTorqueBoost, 0.0f, 0.50f)
            : 1.0f;
    MaintenanceSystem::Update(
        rpm, throttle, speedKmH, FuelSystem::GetOilTemperature(), isEngineOn);
    m_powerMultiplier =
        std::clamp(EngineModel::GetDriveTorqueFactor() * sportTorque *
                       MaintenanceSystem::GetPowerFactor(),
                   0.0f, 4.50f);
    VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(veh, m_powerMultiplier);

    LaunchControl::Update(data, m_manualGear, m_simulatedClutch, throttle,
                          m_absBrake, forwardSpeed, isEngineOn, automaticMode);

    // ── Stall detection ───────────────────────────────────────────────────
    const bool shiftStall = GearboxSystem::ConsumeStallRequest();
    const bool automaticStall =
        automaticMode && AutomaticGearbox::ConsumeStallRequest();
    if ((engineStall || shiftStall || automaticStall) && isEngineOn) {
        m_stallPending = true;
        Renderer::ShowNotification("Engine Stalled! (Drivetrain load)");
        LOG_WARN(Physics,
                 "Drivetrain stall: gear=%d spd=%.1fkm/h load=%.3f heat=%.3f",
                 m_manualGear, speedKmH, EngineModel::GetLoad(),
                 ClutchSystem::GetHeat());
    }

    // ── Fuel stall ────────────────────────────────────────────────────────
    const bool fuelStall =
        FuelSystem::Update(veh, data, throttle, rpm, isEngineOn, speedKmH,
                           m_manualGear, clutchEngagement);
    if (traceFrame)
        LOG_INFO(Script, "TRACE v1 stage=fuel fuel=%.3f stall=%d",
                 FuelSystem::GetFuelLevel(), fuelStall ? 1 : 0);
    if (fuelStall && isEngineOn) {
        m_stallPending = true;
        LOG_ERROR(Fuel, "Fuel stall! fuel=%.3f oilTemp=%.3f",
                  FuelSystem::GetFuelLevel(), FuelSystem::GetOilTemperature());
        Renderer::ShowNotification("~r~OUT OF FUEL! Engine stopped.");
    }

    // ── Lights ────────────────────────────────────────────────────────────
    LightsLogic::Update(veh, data, m_manualGear,
                        InputHandler::GetSmoothedBrake(), throttle);

    // ── Second memory write (ensures final state is consistent) ───────────
    if (automaticMode) {
        AutomaticGearbox::ApplyToMemory(veh, data, m_manualGear,
                                        controlThrottle);
    } else {
        GearLogic::ApplyToMemory(veh, data, m_manualGear, maxGear,
                                 m_simulatedClutch, throttle, speedKmH);
        ClutchSystem::ApplyToVehicle(data, m_manualGear, forwardSpeed);
    }

    if (traceFrame) {
        LOG_INFO(Script,
                 "TRACE v1 stage=frame-complete memGear=%u next=%u "
                 "rpm=%.3f clutch=%.3f",
                 static_cast<unsigned>(data.GetGear()),
                 static_cast<unsigned>(data.GetNextGear()), data.GetRPM(),
                 data.GetClutch());
        m_firstFrameTrace = false;
    }

    if (m_grindWarningTimer > 0)
        --m_grindWarningTimer;
}
