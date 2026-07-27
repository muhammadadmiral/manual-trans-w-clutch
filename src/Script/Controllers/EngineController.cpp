// =============================================================================
// EngineController.cpp — Engine start/stop state machine implementation.
// All logic extracted verbatim from MainScript.cpp baris 395–479, then
// restructured into a proper state machine without removing any behaviour.
// =============================================================================
#include "EngineController.h"

#include "../../Core/Config.h"
#include "../../Core/InputHandler.h"
#include "../../Core/ModLogger.h"
#include "../../Core/Renderer.h"
#include "../../Vehicle/Gearbox/Automatic/AutomaticGearbox.h"
#include "../../Vehicle/VehicleData.h"
#include "../../Vehicle/VehicleProfile.h"
#include "../DrivingEventBus.h"
#include "../../../sdk/inc/natives.h"

#include <algorithm>
#include <cmath>

void EngineController::Reset() {
    m_state             = State::Off;
    m_engineStartTick   = 0;
    m_lastAttemptTick   = 0;
    m_starterRequiredMs = 450;
    m_starterFatigue    = 0.0f;
}

void EngineController::Initialize(Vehicle veh,
                                   VehicleProfile::Drivetrain profile,
                                   bool coldStart, bool actualEngineOn) {
    (void)profile;
    m_engineStartTick   = 0;
    m_lastAttemptTick   = 0;
    m_starterRequiredMs = 450;
    m_starterFatigue    = 0.0f;

    if (coldStart) {
        m_state = State::Off;
        VEHICLE::SET_VEHICLE_ENGINE_ON(veh, FALSE, TRUE, TRUE);
        LOG_INFO(Script, "Cold start required — engine forced OFF for vehicle %d", veh);
    } else {
        m_state = actualEngineOn ? State::Running : State::Off;
    }
}

void EngineController::Update(Vehicle veh,
                               VehicleProfile::Drivetrain profile,
                               bool actualEngineOn, int manualGear) {
    // ── Engine toggle key ─────────────────────────────────────────────────
    if (InputHandler::IsEngineJustPressed()) {
        bool canStart = true;
        if (m_state != State::Running && m_state != State::Cranking &&
            VehicleData::IsInitialized() &&
            Config::StarterInterlock && Config::TransmissionMode != 0) {
            const bool automaticStart =
                VehicleProfile::ForcesAutomatic(profile) ||
                Config::TransmissionMode == 1;
            if (automaticStart) {
                const auto selector = AutomaticGearbox::GetSelector();
                const bool safeSelector =
                    profile == VehicleProfile::Drivetrain::ScooterCVT ||
                    selector == AutomaticGearbox::Selector::Park ||
                    selector == AutomaticGearbox::Selector::Neutral;
                const bool brakeReady =
                    !Config::AutomaticStartRequiresBrake ||
                    InputHandler::GetSmoothedBrake() >= 0.25f;
                canStart = safeSelector && brakeReady;
            } else {
                canStart = manualGear == 0 || InputHandler::IsClutchDown();
            }
        }

        if (canStart) {
            if (m_state == State::Running || m_state == State::Cranking) {
                // Turn engine OFF
                m_state = State::Off;
                VEHICLE::SET_VEHICLE_ENGINE_ON(veh, FALSE, TRUE, TRUE);
                LOG_INFO(Script, "Engine key -> OFF");
            } else {
                // Try to start
                if (profile == VehicleProfile::Drivetrain::Electric) {
                    m_state = State::Running;
                    VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(veh, 1.0f);
                    VEHICLE::SET_VEHICLE_ENGINE_ON(veh, TRUE, TRUE, TRUE);
                    DrivingEventBus::EventData event{};
                    event.vehicle = veh;
                    DrivingEventBus::Publish(
                        DrivingEventBus::Event::EngineStart, event);
                    LOG_INFO(Script, "EV power -> READY");
                } else {
                    const ULONGLONG now = GetTickCount64();
                    if (m_lastAttemptTick != 0 &&
                        now - m_lastAttemptTick < 10000) {
                        m_starterFatigue = std::min(4.0f, m_starterFatigue + 1.0f);
                    } else {
                        m_starterFatigue = std::max(0.0f, m_starterFatigue - 0.5f);
                    }
                    m_lastAttemptTick = now;
                    m_starterRequiredMs =
                        450 + static_cast<ULONGLONG>(m_starterFatigue * 280.0f);
                    m_state = State::Cranking;
                    m_engineStartTick = now;
                    VEHICLE::SET_VEHICLE_ENGINE_ON(veh, TRUE, FALSE, TRUE);
                    LOG_INFO(Script,
                             "Engine key -> STARTING drain=%.1f crankTarget=%llums",
                             m_starterFatigue, m_starterRequiredMs);
                }
            }
        } else {
            Renderer::ShowNotification(
                "~r~Starter interlock:~w~ clutch / brake dan posisi gear belum aman");
        }
    } else if (m_state == State::Cranking) {
        const ULONGLONG crankMs = GetTickCount64() - m_engineStartTick;
        if (actualEngineOn && crankMs >= m_starterRequiredMs) {
            m_state = State::Running;
            VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(veh, 1.0f);
            DrivingEventBus::EventData event{};
            event.vehicle = veh;
            DrivingEventBus::Publish(
                DrivingEventBus::Event::EngineStart, event);
            LOG_INFO(Script, "Starter completed in %llums", crankMs);
        } else if (crankMs > m_starterRequiredMs + 2050) {
            m_state = actualEngineOn ? State::Running : State::Off;
            LOG_WARN(Script, "Starter timeout actual=%d", actualEngineOn ? 1 : 0);
        }
    } else if (m_state == State::Off && actualEngineOn) {
        // Game AI turned it back on — force our state.
        VEHICLE::SET_VEHICLE_ENGINE_ON(veh, FALSE, TRUE, TRUE);
        LOG_DEBUG(Script,
                  "Engine mismatch corrected: we=OFF game=ON -> forcing OFF");
    } else if (m_state == State::Running && !actualEngineOn) {
        const float engineHealth = VEHICLE::GET_VEHICLE_ENGINE_HEALTH(veh);
        if (!VEHICLE::IS_VEHICLE_DRIVEABLE(veh, TRUE) || engineHealth <= 0.0f) {
            m_state = State::Stalled;
            LOG_WARN(Script, "Engine unavailable health=%.1f", engineHealth);
        } else {
            // Flag engine native kadang drop sesaat saat downshift ekstrem.
            VEHICLE::SET_VEHICLE_ENGINE_ON(veh, TRUE, TRUE, TRUE);
        }
    } else if (m_state == State::Stalled && actualEngineOn) {
        // Recovery dari stall kalau game somehow restart engine
        m_state = State::Off;
    }
}

void EngineController::ForceStall(Vehicle veh, const char* reason) {
    m_state = State::Stalled;
    VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(veh, 0.0f);
    VEHICLE::SET_VEHICLE_ENGINE_ON(veh, FALSE, TRUE, TRUE);
    DrivingEventBus::EventData event{};
    event.vehicle = veh;
    DrivingEventBus::Publish(DrivingEventBus::Event::EngineStall, event);
    LOG_WARN(Physics, "Engine stall forced: %s", reason);
}
