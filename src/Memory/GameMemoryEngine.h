#pragma once

#include <Windows.h>
#include <cstdint>
#include <initializer_list>
#include <string>

// Forward declarations
struct VehicleOffsets;

namespace GameMemory {

struct WheelTelemetry {
  float angularVelocity = 0.0f;
  float load = 0.0f;
  float brakePressure = 0.0f;
  float power = 0.0f;
  bool valid = false;
};

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

  float GetDriveForce() const;
  float GetDriveInertia() const;
  float GetDriveMaxFlatVel() const;
  void SetDriveForce(float force);

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
  float GetGearRatio(uint8_t gearIndex) const;
  float GetClutch() const;
  float GetRPM() const;
  float GetThrottle() const;
  float GetThrottlePedal() const;
  float GetTransmissionDriveForce() const;
  float GetTransmissionDriveMaxFlatVel() const;
  uint8_t GetWheelCount() const;
  WheelTelemetry GetWheelTelemetry(uint8_t index) const;
  float GetFuelLevel() const;
  float GetHoverTransformRatioLerp() const;
  uint8_t GetLightsBroken() const;
  uint8_t GetLightsVisuallyBroken() const;

  void SetGear(uint8_t gear);
  void SetNextGear(uint8_t gear);
  void SetTopGear(uint8_t gear);
  void SetClutch(float clutch);
  void SetRPM(float rpm);
  void SetThrottle(float throttle);
  void SetThrottlePedal(float throttle);
  void SetLightsBroken(uint8_t state);
  void SetLightsVisuallyBroken(uint8_t state);

private:
  uintptr_t m_address;
  const VehicleOffsets *m_offsets;
};

} // namespace GameMemory
