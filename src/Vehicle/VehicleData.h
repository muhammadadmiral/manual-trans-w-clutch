// Facade memory kendaraan. Modul domain gak boleh pegang pointer mentah.
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
    IniFallback, // Loaded from melar-transmission.ini
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
                                  bool    isRevving,
                                  uint8_t maxGear);

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
    float   GetThrottle()                const;
    float   GetThrottlePedal()           const;
    float   GetDriveInertia()            const;
    float   GetClutchChangeRateScaleUpShift() const;
    float   GetClutchChangeRateScaleDownShift() const;
    float   GetDriveMaxFlatVel()         const;
    float   GetInitialDriveMaxFlatVel()  const;
    uint8_t GetWheelCount()              const;
    GameMemory::WheelTelemetry GetWheelTelemetry(uint8_t index) const;
    float   GetDriveForce()              const;
    float   GetInitialDriveForce()       const;
    float   GetOriginalDriveForce()      const;
    float   GetFuelLevel()               const;
    uint8_t GetLightsBroken()            const;
    uint8_t GetLightsVisuallyBroken()    const;
    float   GetHoverTransformRatioLerp() const;
    float   GetGearRatio(uint8_t gear)   const;

    // Setters — return false when the underlying write would be unsafe
    bool SetGear(uint8_t gear);
    bool SetNextGear(uint8_t gear);
    bool SetTopGear(uint8_t gear);
    bool SetClutch(float clutch);
    bool SetRPM(float rpm);
    bool SetThrottle(float throttle);
    bool SetThrottlePedal(float throttle);
    bool SetDriveForce(float force);
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
    // Cache for original handling values
    float m_originalDriveForce = -1.0f;

    // Helpers
    static bool ResolveOffsetsByPattern(VehicleOffsets& result);
    static bool LoadOffsetsFromIni     (HMODULE pluginModule, VehicleOffsets& result);
    static void SaveOffsetsToIni       (HMODULE pluginModule, const VehicleOffsets& offsets);
    static bool AreOffsetsSane         (const VehicleOffsets& value);
};
