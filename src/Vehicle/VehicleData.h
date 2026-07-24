#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>

#include "../Memory/GameMemoryEngine.h"
#include "../Memory/OffsetResolver.h"

enum class VehicleOffsetSource {
  Uninitialized,
  PatternScan,
  IniFallback,
  Calibration
};

enum class CalibrationState {
  None,
  WaitingForEngineOff,
  ScanningEngineOff,
  WaitingForEngineOn,
  ScanningEngineOn,
  WaitingForRev,
  ScanningRev,
  Done,
  Failed
};

class VehicleData {
public:
  // ── Static lifecycle (one-time per session) ──────────────────────────────
  // Call once after ScriptHookV has initialized the script thread.
  static bool Initialize(HMODULE pluginModule);
  static void ResetCalibration();
  static void UpdateCalibration(HMODULE pluginModule, int vehicleHandle,
                                bool isEngineOn, bool isRevving);
  static CalibrationState GetCalibrationState();
  static bool IsInitialized();
  static VehicleOffsetSource GetOffsetSource();
  static const char *GetOffsetSourceName();
  static const VehicleOffsets &GetResolvedOffsets();
  static std::string GetGameBuildVersion();

  static const std::string &GetLastFailureReason() {
    return lastFailureReason;
  }

  // ── Per-vehicle instance ─────────────────────────────────────────────────
  // Construct with a ScriptHookV vehicle handle.
  explicit VehicleData(int vehicleHandle);

  bool IsValid() const;
  bool HasPlausibleLayout(int maxGear) const;

  // ── Core Getters ─────────────────────────────────────────────────────────
  uint8_t GetGear() const;
  uint8_t GetNextGear() const;
  uint8_t GetTopGear() const;
  float   GetClutch() const;
  float   GetRPM() const;
  float   GetDriveForce() const;
  float   GetFuelLevel() const;
  uint8_t GetLightsBroken() const;
  uint8_t GetLightsVisuallyBroken() const;
  float   GetHoverTransformRatioLerp() const;
  float   GetGearRatio(uint8_t gear) const;

  // ── Core Setters ─────────────────────────────────────────────────────────
  bool SetGear(uint8_t gear);
  bool SetNextGear(uint8_t gear);
  bool SetClutch(float clutch);
  bool SetRPM(float rpm);
  bool SetLightsBroken(uint8_t brokenState);
  bool SetLightsVisuallyBroken(uint8_t brokenState);

private:
  GameMemory::CVehicle m_vehicle = GameMemory::CVehicle(0, nullptr);
  bool isValid = false;

  bool CanRead(uint32_t offset, size_t size) const;
  bool CanWrite(uint32_t offset, size_t size) const;

  // ── Shared static state ──────────────────────────────────────────────────
  static VehicleOffsets resolvedOffsets;
  static VehicleOffsetSource offsetSource;
  static bool initialized;
  static std::string lastFailureReason;

  static bool ResolveOffsetsByPattern(VehicleOffsets &result);
  static bool LoadOffsetsFromIni(HMODULE pluginModule, VehicleOffsets &result);
  static void SaveOffsetsToIni(HMODULE pluginModule, const VehicleOffsets &offsets);
  static bool AreOffsetsSane(const VehicleOffsets &value);

  static CalibrationState calibState;
  static std::vector<uint32_t> candidateOffsets;
};