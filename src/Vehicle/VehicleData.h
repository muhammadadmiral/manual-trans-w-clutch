// =============================================================================
// VehicleData.h
// Owns the resolved CVehicle memory offsets (one set per GTA V session) and
// provides per-vehicle instance accessors for reading / writing those fields.
//
// ── Responsibility split ──────────────────────────────────────────────────────
//   VehicleData (this file)
//     • Initialize()        — try AOB → INI → start calibration
//     • IsInitialized()     — whether offsets are ready to use
//     • GetResolvedOffsets()
//     • Per-vehicle instance: GetRPM / SetGear / HasPlausibleLayout / etc.
//
//   CalibrationEngine (src/Memory/CalibrationEngine.h/cpp)
//     • State machine: WaitingForEngineOff → … → Done / Failed
//     • SearchGearLayout — two-pass robust search
//
//   OffsetResolver   (src/Memory/OffsetResolver.h/cpp)
//     • AOB pattern scan (used first by Initialize)
//
// Call sequence (MainScript.cpp):
//   1. VehicleData::Initialize(hmod)     — once per session
//   2. while !IsInitialized():
//        VehicleData::UpdateCalibration(...) — drives CalibrationEngine
//   3. VehicleData data(vehicleHandle)   — per-frame instance
//      data.GetRPM() / data.SetGear() / …
// =============================================================================
#pragma once

#include "../Memory/CalibrationEngine.h" // CalibrationState enum
#include "../Memory/GameMemoryEngine.h"
#include "../Memory/OffsetResolver.h"    // VehicleOffsets

#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
enum class VehicleOffsetSource {
    Uninitialized,
    PatternScan, // AOB scan succeeded
    IniFallback, // Loaded from manual-trans.ini
    Calibration, // Interactive calibration
};

// ---------------------------------------------------------------------------
class VehicleData {
public:
    // ── Static lifecycle ─────────────────────────────────────────────────────
    // Call once after ScriptHookV has started the script thread.
    // Returns true whether offsets are ready (PatternScan / IniFallback) OR
    // calibration has been launched (returns true, IsInitialized() == false).
    static bool Initialize(HMODULE pluginModule);

    // Must be called every frame while IsInitialized() == false.
    static void UpdateCalibration(HMODULE pluginModule,
                                  int     vehicleHandle,
                                  bool    isEngineOn,
                                  bool    isRevving);

    static void ResetCalibration();

    // ── Query ─────────────────────────────────────────────────────────────────
    static bool                IsInitialized();
    static VehicleOffsetSource GetOffsetSource();
    static const char*         GetOffsetSourceName();
    static const VehicleOffsets& GetResolvedOffsets();
    static std::string           GetGameBuildVersion();
    static const std::string&    GetLastFailureReason();

    // CalibrationEngine delegation
    static CalibrationState GetCalibrationState();
    static size_t           GetCalibrationCandidateCount();

    // ── Per-vehicle instance ──────────────────────────────────────────────────
    explicit VehicleData(int vehicleHandle);

    bool IsValid()               const;
    bool HasPlausibleLayout(int maxGear) const;

    // Getters
    uint8_t GetGear()                    const;
    uint8_t GetNextGear()                const;
    uint8_t GetTopGear()                 const;
    float   GetClutch()                  const;
    float   GetRPM()                     const;
    float   GetDriveForce()              const;
    float   GetFuelLevel()               const;
    uint8_t GetLightsBroken()            const;
    uint8_t GetLightsVisuallyBroken()    const;
    float   GetHoverTransformRatioLerp() const;
    float   GetGearRatio(uint8_t gear)   const;

    // Setters — return false when the underlying write would be unsafe
    bool SetGear(uint8_t gear);
    bool SetNextGear(uint8_t gear);
    bool SetClutch(float clutch);
    bool SetRPM(float rpm);
    bool SetLightsBroken(uint8_t state);
    bool SetLightsVisuallyBroken(uint8_t state);

private:
    GameMemory::CVehicle m_vehicle = GameMemory::CVehicle(0, nullptr);
    bool                 m_isValid = false;

    bool CanRead (uint32_t offset, size_t size) const;
    bool CanWrite(uint32_t offset, size_t size) const;

    // ── Shared static state ──────────────────────────────────────────────────
    static VehicleOffsets      resolvedOffsets;
    static VehicleOffsetSource offsetSource;
    static bool                initialized;
    static std::string         lastFailureReason;

    // ── Private helpers ───────────────────────────────────────────────────────
    static bool ResolveOffsetsByPattern(VehicleOffsets& result);
    static bool LoadOffsetsFromIni     (HMODULE pluginModule, VehicleOffsets& result);
    static void SaveOffsetsToIni       (HMODULE pluginModule, const VehicleOffsets& offsets);
    static bool AreOffsetsSane         (const VehicleOffsets& value);
};