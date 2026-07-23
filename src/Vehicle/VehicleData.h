#pragma once

#include <Windows.h>
#include <cstdint>

struct VehicleOffsets {
  uint32_t Gear = 0;
  uint32_t NextGear = 0;
  uint32_t Clutch = 0;
  uint32_t RPM = 0;

  bool IsComplete() const;
};

enum class VehicleOffsetSource { Uninitialized, PatternScan, IniFallback };

class VehicleData {
public:
  // Call once after ScriptHookV has initialized the script thread.
  static bool Initialize(HMODULE pluginModule);
  static bool IsInitialized();
  static VehicleOffsetSource GetOffsetSource();
  static const char *GetOffsetSourceName();
  static const VehicleOffsets &GetResolvedOffsets();

  // Short, human-readable reason the last Initialize() call failed.
  // Only meaningful after Initialize() has returned false.
  static const char *GetLastFailureReason();

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
  static bool AreOffsetsSane(const VehicleOffsets &value);

  bool CanRead(uint32_t offset, size_t size) const;
  bool CanWrite(uint32_t offset, size_t size) const;
};