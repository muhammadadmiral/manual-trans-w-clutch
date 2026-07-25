#pragma once

#include <Windows.h>
#include <cstdint>
#include <initializer_list>
#include <string>

// Forward declarations
struct VehicleOffsets;

namespace GameMemory {

// A robust pointer traversal helper that validates memory at every step.
// For example, to read: [[[worldPtr] + 0x8] + 0xD28]
template <typename T>
bool ReadPointerChain(uintptr_t base,
                      const std::initializer_list<ptrdiff_t> &offsets,
                      T &outValue) {
  if (base == 0)
    return false;
  uintptr_t current = base;

  auto it = offsets.begin();
  while (it != offsets.end()) {
    ptrdiff_t offset = *it;
    ++it;

    if (it == offsets.end()) {
      // Last offset, read the actual value
      __try {
        outValue = *reinterpret_cast<T *>(current + offset);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    } else {
      // Intermediate offset, read the next pointer
      __try {
        current = *reinterpret_cast<uintptr_t *>(current + offset);
        if (current == 0)
          return false;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }
  }
  return false;
}

// Memory abstraction for CHandlingData
class CHandlingData {
public:
  explicit CHandlingData(uintptr_t address, const VehicleOffsets *offsets)
      : m_address(address), m_offsets(offsets) {}

  bool IsValid() const { return m_address != 0 && m_offsets != nullptr; }

  // Returns the array of GearRatios.
  // In GTA V, gear ratios are an array of floats, usually up to 8 gears.
  float GetGearRatio(uint8_t gearIndex) const;

  float GetDriveForce() const;

private:
  uintptr_t m_address;
  const VehicleOffsets *m_offsets;
};

// Memory abstraction for CVehicle
class CVehicle {
public:
  explicit CVehicle(uintptr_t address, const VehicleOffsets *offsets)
      : m_address(address), m_offsets(offsets) {}

  bool IsValid() const { return m_address != 0 && m_offsets != nullptr; }
  uintptr_t GetAddress() const { return m_address; }

  CHandlingData GetHandlingData() const;

  uint8_t GetGear() const;
  uint8_t GetNextGear() const;
  uint8_t GetTopGear() const;
  float GetClutch() const;
  float GetRPM() const;
  float GetFuelLevel() const;
  float GetHoverTransformRatioLerp() const;
  uint8_t GetLightsBroken() const;
  uint8_t GetLightsVisuallyBroken() const;

  void SetGear(uint8_t gear);
  void SetNextGear(uint8_t gear);
  void SetTopGear(uint8_t gear);
  void SetClutch(float clutch);
  void SetRPM(float rpm);
  void SetLightsBroken(uint8_t state);
  void SetLightsVisuallyBroken(uint8_t state);

private:
  uintptr_t m_address;
  const VehicleOffsets *m_offsets;
};

} // namespace GameMemory
