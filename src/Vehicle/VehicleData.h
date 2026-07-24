#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>

struct VehicleOffsets {
  uint32_t Gear = 0;
  uint32_t NextGear = 0;
  uint32_t Clutch = 0;
  uint32_t RPM = 0;

  bool IsComplete() const;
};

enum class VehicleOffsetSource { Uninitialized, PatternScan, IniFallback, Calibration };

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
  // Call once after ScriptHookV has initialized the script thread.
  static bool Initialize(HMODULE pluginModule);
  static void ResetCalibration();
  static void UpdateCalibration(HMODULE pluginModule, int vehicleHandle, bool isEngineOn, bool isRevving);
  static CalibrationState GetCalibrationState();
  static bool IsInitialized();
  static VehicleOffsetSource GetOffsetSource();
  static const char *GetOffsetSourceName();
  static const VehicleOffsets &GetResolvedOffsets();

  // Short, human-readable reason the last Initialize() call failed.
  // Only meaningful after Initialize() has returned false.
  static const char *GetLastFailureReason();

  // "A.B.C.D" file version of the running GTA5.exe (e.g. "1.0.1013.20"),
  // or "" if it couldn't be read. Also used internally: when the AOB scan
  // fails and AllowIniFallback=1, Initialize() looks for a
  // [Offsets.<this version>] section before falling back to the generic
  // [Offsets] section, so you can keep several builds' verified offsets
  // side by side in one ini.
  static std::string GetGameBuildVersion();

  explicit VehicleData(int vehicleHandle);

  bool IsValid() const;
  bool HasPlausibleLayout(int maxGear) const;

  uint8_t GetGear() const;
  uint8_t GetNextGear() const;
  float GetClutch() const;
  float GetRPM() const;

  bool SetGear(uint8_t gear);
  bool SetNextGear(uint8_t gear);
  bool SetClutch(float clutch);
  bool SetRPM(float rpm);

private:
  BYTE *baseAddress = nullptr;

  static VehicleOffsets resolvedOffsets;
  static VehicleOffsetSource offsetSource;
  static bool initialized;
  static const char *lastFailureReason;

  static bool ResolveOffsetsByPattern(VehicleOffsets &result);
  static bool LoadOffsetsFromIni(HMODULE pluginModule, VehicleOffsets &result);
  static void SaveOffsetsToIni(HMODULE pluginModule, const VehicleOffsets &offsets);
  static bool AreOffsetsSane(const VehicleOffsets &value);

  // Calibration state
  static CalibrationState calibState;
  static std::vector<uint32_t> candidateOffsets;

  bool CanRead(uint32_t offset, size_t size) const;
  bool CanWrite(uint32_t offset, size_t size) const;
};