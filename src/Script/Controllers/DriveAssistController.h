// =============================================================================
// DriveAssistController.h — One owner for TCS, ABS, launch control, ESC and
// rollover mitigation.
// =============================================================================
#pragma once

#include "../../Vehicle/VehicleData.h"

using Vehicle = int;

class DriveAssistController {
public:
    enum class BrakeCorner {
        None,
        FrontLeft,
        FrontRight,
        RearLeft,
        RearRight
    };

    struct AssistState {
        bool tcsActive = false;
        bool absActive = false;
        bool escActive = false;
        bool lcArmed = false;
        bool rollWarning = false;
        bool hillHoldActive = false;
        float tcsThrottle = 1.0f;
        float absBrake = 0.0f;
        float escBrake = 0.0f;
        float escThrottleCut = 0.0f;
        float launchCut = 0.0f;
        float torqueIntervention = 0.0f;
        float lateralVelocity = 0.0f;
        float lateralAcceleration = 0.0f;
        float slipAngleDeg = 0.0f;
        float yawRate = 0.0f;
        float desiredYawRate = 0.0f;
        float stabilityError = 0.0f;
        float rollAngleDeg = 0.0f;
        BrakeCorner brakeCorner = BrakeCorner::None;
    };

    // Mutates throttle/brake in place before the drivetrain applies controls.
    void Update(Vehicle veh, VehicleData& data, int gear,
                float clutchDisengagement, float& throttle, float& brake,
                float forwardSpeed, bool engineOn, bool automaticMode);
    void Reset();

    const AssistState& GetState() const { return m_state; }

private:
    void ApplySelectiveBrake(Vehicle veh, float severity, float yawError,
                             bool oversteer);

    AssistState m_state{};
    Vehicle m_vehicle = 0;
    float m_previousLateralVelocity = 0.0f;
    float m_filteredLateralAcceleration = 0.0f;
    float m_filteredSlipAngle = 0.0f;
    bool m_velocityInitialized = false;
    bool m_tcsWasActive = false;
    bool m_absWasActive = false;
    bool m_escWasActive = false;
    bool m_lcWasArmed = false;
    bool m_lcWasLimiting = false;
    bool m_rollWasActive = false;
};
