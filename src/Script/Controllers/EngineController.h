// =============================================================================
// EngineController.h — Engine start/stop state machine.
// Handles cold start, starter fatigue, EV mode, and mismatch correction.
// =============================================================================
#pragma once

#include "../../Vehicle/VehicleProfile.h"
#include <Windows.h>

using Vehicle = int;

class EngineController {
public:
    // ── State machine ─────────────────────────────────────────────────────────
    enum class State { Off, Cranking, Running, Stalled };

    // Reset semua state ke default.
    void Reset();

    // Panggil saat baru masuk kendaraan. ColdStart akan memaksa engine OFF.
    void Initialize(Vehicle veh, VehicleProfile::Drivetrain profile,
                    bool coldStart, bool actualEngineOn);

    // Per-frame update. Menangani input start/stop dan koreksi mismatch.
    void Update(Vehicle veh, VehicleProfile::Drivetrain profile,
                bool actualEngineOn, int manualGear);

    // Dipanggil dari luar saat drivetrain stall (clutch/gearbox/fuel).
    void ForceStall(Vehicle veh, const char* reason);

    // ── Queries ───────────────────────────────────────────────────────────────
    bool  IsOn()         const { return m_state == State::Running; }
    bool  IsStarting()   const { return m_state == State::Cranking; }
    bool  IsStalled()    const { return m_state == State::Stalled; }
    State GetState()     const { return m_state; }
    float GetStarterFatigue() const { return m_starterFatigue; }

private:
    State     m_state             = State::Off;
    ULONGLONG m_engineStartTick   = 0;
    ULONGLONG m_lastAttemptTick   = 0;
    ULONGLONG m_starterRequiredMs = 450;
    float     m_starterFatigue    = 0.0f;
};
