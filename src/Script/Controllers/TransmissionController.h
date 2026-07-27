// =============================================================================
// TransmissionController.h — Core physics loop: mode switching, clutch/gear
// calculation, pedal model, memory writes, and stall detection.
// The biggest controller — heart of the drivetrain simulation.
// =============================================================================
#pragma once

#include "../../Vehicle/VehicleData.h"
#include "../../Vehicle/VehicleProfile.h"
#include <Windows.h>

using Vehicle = int;

class DriveAssistController;

class TransmissionController {
public:
    void Update(Vehicle veh, VehicleData& data,
                VehicleProfile::Drivetrain profile,
                bool isEngineOn, bool workshopOpen,
                float vehicleSpeed, float forwardSpeed, int maxGear,
                DriveAssistController& assist);
    void Reset();

    // ── Queries ───────────────────────────────────────────────────────────
    int   GetManualGear()       const { return m_manualGear; }
    int   GetMode()             const { return m_activeMode; }
    float GetSimulatedClutch()  const { return m_simulatedClutch; }
    float GetDriveThrottle()    const { return m_driveThrottle; }
    float GetBrake()            const { return m_absBrake; }
    float GetPowerMultiplier()  const { return m_powerMultiplier; }
    int   GetGrindWarningTimer() const { return m_grindWarningTimer; }

    // Consume drivetrain stall event (returns true once, then clears).
    bool  ConsumedStallEvent();

    // Was this the first controlled frame? (for trace logging)
    bool  WasFirstFrame() const { return m_wasFirstFrame; }

private:
    int       m_manualGear           = 0;
    int       m_activeMode           = -1;
    ULONGLONG m_automaticClutchUntil = 0;
    bool      m_patchFailureShown    = false;
    bool      m_firstFrameTrace      = true;
    bool      m_wasFirstFrame        = false;
    int       m_grindWarningTimer    = 0;
    bool      m_stallPending         = false;

    // Cached per-frame results
    float     m_simulatedClutch      = 0.0f;
    float     m_driveThrottle        = 0.0f;
    float     m_absBrake             = 0.0f;
    float     m_powerMultiplier      = 1.0f;
};
