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
  // Diagnostic: how many memory candidates survived the last calibration
  // filtering step. 0 means the scan window doesn't contain the field at
  // all for this game build - widen kCalibScanStart/End below.
  static size_t GetCalibrationCandidateCount();
  static bool IsInitialized();
  static VehicleOffsetSource GetOffsetSource();
  static const char *GetOffsetSourceName();
  static const VehicleOffsets &GetResolvedOffsets();
  static std::string GetGameBuildVersion();

  static const std::string &GetLastFailureReason() { return lastFailureReason; }

  // ── Per-vehicle instance ─────────────────────────────────────────────────
  // Construct with a ScriptHookV vehicle handle.
  explicit VehicleData(int vehicleHandle);

  bool IsValid() const;
  bool HasPlausibleLayout(int maxGear) const;

  // ── Core Getters ─────────────────────────────────────────────────────────
  uint8_t GetGear() const;
  uint8_t GetNextGear() const;
  uint8_t GetTopGear() const;
  float GetClutch() const;
  float GetRPM() const;
  float GetDriveForce() const;
  float GetFuelLevel() const;
  uint8_t GetLightsBroken() const;
  uint8_t GetLightsVisuallyBroken() const;
  float GetHoverTransformRatioLerp() const;
  float GetGearRatio(uint8_t gear) const;

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
  static void SaveOffsetsToIni(HMODULE pluginModule,
                               const VehicleOffsets &offsets);
  static bool AreOffsetsSane(const VehicleOffsets &value);

  static bool SearchGearLayout(const GameMemory::CVehicle &calibVehicle,
                               uint32_t searchLo, uint32_t searchHi,
                               uint32_t &outGearOffset,
                               uint32_t &outNextGearOffset,
                               uint32_t &outTopGearOffset,
                               uint32_t &outRatiosOffset);

  static CalibrationState calibState;
  static std::vector<uint32_t> candidateOffsets;
  // Value each surviving candidate read at idle, captured in the
  // engine-on stage. Used to detect rev by relative increase instead of
  // a hardcoded absolute band (RPM's numeric scale isn't guaranteed to
  // match legacy GTA V's 0-1 normalization on every build).
  static std::vector<float> candidateIdleValues;
  // 0 = not currently waiting; otherwise the tick count when the current
  // settle wait started (see kEngineOffSettleMs etc below).
  static uint64_t phaseEnterTick;

  // Scan window for calibration, in bytes from the CVehicle base.
  // GTA V legacy CVehicle is roughly ~0xE00-0x1200 bytes depending on
  // build; GTA V Enhanced grew noticeably on top of that. If calibration
  // keeps reporting 0 candidates at the "engine off" stage, the field simply
  // isn't inside this window for your build - widen it (e.g. to 0x1800) and
  // rebuild.
  static constexpr uint32_t kCalibScanStart = 0x600;
  static constexpr uint32_t kCalibScanEnd = 0x1400;

  // How long to wait, after the state we care about becomes true, before
  // actually snapshotting memory. RPM doesn't jump instantly - it decays
  // to 0 over time after the engine cuts, takes a moment to stabilize at
  // idle, and takes a moment to climb when you rev. Scanning on the very
  // next frame after the state flips catches a mid-transition value, not
  // the settled one - this was the actual root cause of calibration
  // reliably reaching "Scanning..." and then failing.
  static constexpr uint64_t kEngineOffSettleMs = 2000;
  static constexpr uint64_t kIdleSettleMs = 2500;
  static constexpr uint64_t kRevSettleMs = 1500;

  // Window (in bytes) around the found RPM offset to search for the
  // Gear/NextGear/TopGear/GearRatios cluster. Deliberately wide and NOT
  // tied to any one game build's exact byte layout - see the search
  // itself in UpdateCalibration for why.
  static constexpr uint32_t kGearSearchBefore = 0x400;
  static constexpr uint32_t kGearSearchAfter = 0x100;
};