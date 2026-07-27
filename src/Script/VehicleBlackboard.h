// =============================================================================
// VehicleBlackboard.h — Per-frame shared state populated once at the top of the
// game loop, readable by every controller without parameter passing.
// =============================================================================
#pragma once

#include "../Vehicle/VehicleProfile.h"
#include "../Vehicle/VehicleData.h"
#include "../../sdk/inc/natives.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>

struct VehicleBlackboard {
    // ── Identity ──────────────────────────────────────────────────────────────
    Vehicle vehicle            = 0;
    Ped     playerPed          = 0;
    Hash    modelHash          = 0;

    // ── Drivetrain ────────────────────────────────────────────────────────────
    int     maxGear            = 0;
    VehicleProfile::Drivetrain profile = VehicleProfile::Drivetrain::Standard;
    bool    isBike             = false;
    bool    isQuadbike         = false;
    bool    isElectric         = false;
    bool    isScooter          = false;
    bool    isUtility          = false;

    // ── Kinematics ────────────────────────────────────────────────────────────
    float   vehicleSpeed       = 0.0f;  // unsigned magnitude (m/s)
    float   speedKmH           = 0.0f;
    float   forwardSpeed       = 0.0f;  // signed forward component (m/s)

    // ── Engine telemetry ──────────────────────────────────────────────────────
    float   rpm                = 0.0f;  // normalized 0.0–1.0
    bool    actualEngineOn     = false;

    // ── Time ──────────────────────────────────────────────────────────────────
    float   dt                 = 0.0f;  // clamped frame time
    ULONGLONG tickNow          = 0;

    // ── Convenience flags ─────────────────────────────────────────────────────
    bool    workshopOpen       = false;

    // ── Populate ──────────────────────────────────────────────────────────────
    // Call once per frame at the top of the main loop, after vehicle is known.
    void Populate(Vehicle veh, Ped ped, int maxDriveGear) {
        vehicle        = veh;
        playerPed      = ped;
        maxGear        = maxDriveGear;
        modelHash      = ENTITY::GET_ENTITY_MODEL(veh);
        profile        = VehicleProfile::Detect(veh);

        isBike         = VEHICLE::IS_THIS_MODEL_A_BIKE(modelHash) != FALSE;
        isQuadbike     = VEHICLE::IS_THIS_MODEL_A_QUADBIKE(modelHash) != FALSE;
        isElectric     = profile == VehicleProfile::Drivetrain::Electric;
        isScooter      = profile == VehicleProfile::Drivetrain::ScooterCVT;
        isUtility      = profile == VehicleProfile::Drivetrain::UtilitySingleSpeed;

        vehicleSpeed   = ENTITY::GET_ENTITY_SPEED(veh);
        speedKmH       = vehicleSpeed * 3.6f;
        forwardSpeed   = ENTITY::GET_ENTITY_SPEED_VECTOR(veh, TRUE).y;

        rpm            = 0.0f;   // filled after VehicleData is constructed
        actualEngineOn = VEHICLE::GET_IS_VEHICLE_ENGINE_RUNNING(veh) != FALSE;

        dt             = std::clamp(MISC::GET_FRAME_TIME(), 0.001f, 0.05f);
        tickNow        = GetTickCount64();
    }

    // Update RPM after VehicleData is available (separate step because
    // VehicleData construction needs the vehicle handle first).
    void UpdateRPM(const VehicleData& data) {
        rpm = data.GetRPM();
    }
};

// Global instance — populated once per frame by MainScript.
extern VehicleBlackboard g_frame;
