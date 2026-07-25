// =============================================================================
// CalibrationEngine.h
// Interactive calibration state machine — finds CVehicle memory offsets
// when AOB pattern scan and INI fallback both fail.
//
// ── Separation of concerns ────────────────────────────────────────────────────
// This module owns EVERYTHING about how offsets are discovered at runtime:
//   - the state machine (WaitingForEngineOff → … → Done / Failed)
//   - the two-pass SearchGearLayout (pointer-based + direct byte scan)
//   - the per-phase timing constants
//
// VehicleData owns the resolved offsets and delegates calibration here.
// MainScript drives it by calling UpdateCalibration() each frame.
// =============================================================================
#pragma once

#include "../Memory/OffsetResolver.h" // VehicleOffsets

#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// Calibration phases
// ---------------------------------------------------------------------------
enum class CalibrationState : int {
    None             = 0,
    WaitingForEngineOff,   // User must turn engine OFF
    ScanningEngineOff,     // Waiting for RPM to settle at 0
    WaitingForEngineOn,    // User must turn engine ON
    ScanningEngineOn,      // Waiting for idle RPM to stabilize
    WaitingForRev,         // User must hold throttle
    ScanningRev,           // Waiting for RPM to climb under load
    Done,                  // Complete — offsets resolved
    Failed,                // Unrecoverable — check GetLastError()
};

// ---------------------------------------------------------------------------
// CalibrationEngine — purely static interface (no instances)
// ---------------------------------------------------------------------------
namespace CalibrationEngine {

// ── Timing constants (ms) ─────────────────────────────────────────────────
// After a state change, these are how long we wait before snapshotting.
// RPM doesn't transition instantly — decaying to 0 takes ~1-2 s, climbing
// to idle takes a beat, revving needs a second to reach steady-state.
constexpr uint64_t kEngineOffSettleMs = 2000;
constexpr uint64_t kIdleSettleMs      = 2500;
constexpr uint64_t kRevSettleMs       = 1500;

// ── Scan window (byte offsets from CVehicle base) ─────────────────────────
// GTA V Enhanced CVehicle grew past the legacy ~0xE00 mark.
// Widen kScanEnd if the engine-off stage reports 0 candidates.
constexpr uint32_t kScanStart = 0x600;
constexpr uint32_t kScanEnd   = 0x1800;

// ── Query ─────────────────────────────────────────────────────────────────
CalibrationState   GetState();
size_t             GetCandidateCount();
const std::string& GetLastError();

// ── Control ───────────────────────────────────────────────────────────────
void Reset();

// Called every frame while VehicleData::IsInitialized() == false.
// Returns true exactly once when calibration succeeds; on that frame
// outOffsets contains the resolved offsets (RPM / Clutch / Gear / NextGear /
// TopGear / GearRatios).  The caller is responsible for saving to INI.
bool Update(int  vehicleHandle,
            bool isEngineOn,
            bool isRevving,
            VehicleOffsets& outOffsets);

} // namespace CalibrationEngine
