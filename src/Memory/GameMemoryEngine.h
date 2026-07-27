#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct VehicleOffsets;

namespace GameMemory {

struct WheelTelemetry {
  float angularVelocity = 0.0f;
  float load = 0.0f;
  float brakePressure = 0.0f;
  float power = 0.0f;
  bool valid = false;
};

// A validated view of CHandlingData. Address protection is queried when the
// owning CVehicle is bound, not from telemetry getters in the frame loop.
class CHandlingData {
public:
  CHandlingData() = default;

  bool IsValid() const {
    return m_address != 0 && m_offsets != nullptr &&
           m_address >= m_regionStart && m_address < m_regionEnd;
  }

  float GetDriveForce() const;
  float GetDriveInertia() const;
  float GetClutchChangeRateScaleUpShift() const;
  float GetClutchChangeRateScaleDownShift() const;
  float GetDriveMaxFlatVel() const;
  float GetInitialDriveMaxFlatVel() const;
  void SetDriveForce(float force);

private:
  friend class CVehicle;

  CHandlingData(uintptr_t address, const VehicleOffsets *offsets,
                uintptr_t regionStart, uintptr_t regionEnd, bool writable)
      : m_address(address), m_offsets(offsets), m_regionStart(regionStart),
        m_regionEnd(regionEnd), m_writable(writable) {}

  bool CanRead(uint32_t offset, size_t size) const;
  bool CanWrite(uint32_t offset, size_t size) const;

  uintptr_t m_address = 0;
  const VehicleOffsets *m_offsets = nullptr;
  uintptr_t m_regionStart = 0;
  uintptr_t m_regionEnd = 0;
  bool m_writable = false;
};

// CVehicle is rebound when the base pointer/layout changes and periodically
// revalidated. Per-frame reads and writes only perform cached range checks.
class CVehicle {
public:
  explicit CVehicle(uintptr_t address, const VehicleOffsets *offsets,
                    int identity = 0);

  bool IsValid() const { return m_valid; }
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
  bool CanWriteGearRatios() const;
  uint8_t GetWheelCount() const;
  WheelTelemetry GetWheelTelemetry(uint8_t index) const;
  float GetFuelLevel() const;
  float GetHoverTransformRatioLerp() const;
  uint8_t GetLightsBroken() const;
  uint8_t GetLightsVisuallyBroken() const;

  void SetGear(uint8_t gear);
  void SetNextGear(uint8_t gear);
  void SetTopGear(uint8_t gear);
  bool SetGearRatio(uint8_t gearIndex, float ratio);
  void SetClutch(float clutch);
  void SetRPM(float rpm);
  void SetThrottle(float throttle);
  void SetThrottlePedal(float throttle);
  void SetLightsBroken(uint8_t state);
  void SetLightsVisuallyBroken(uint8_t state);

private:
  bool CanReadVehicle(uint32_t offset, size_t size) const;
  bool CanWriteVehicle(uint32_t offset, size_t size) const;

  uintptr_t m_address = 0;
  const VehicleOffsets *m_offsets = nullptr;
  uintptr_t m_vehicleRegionStart = 0;
  uintptr_t m_vehicleRegionEnd = 0;
  bool m_vehicleWritable = false;

  uintptr_t m_handlingAddress = 0;
  uintptr_t m_handlingRegionStart = 0;
  uintptr_t m_handlingRegionEnd = 0;
  bool m_handlingWritable = false;

  uintptr_t m_ratiosAddress = 0;
  uintptr_t m_ratiosRegionStart = 0;
  uintptr_t m_ratiosRegionEnd = 0;
  bool m_ratiosWritable = false;

  std::array<uintptr_t, 16> m_wheelAddresses{};
  std::array<uintptr_t, 16> m_wheelRegionStarts{};
  std::array<uintptr_t, 16> m_wheelRegionEnds{};
  uint8_t m_wheelCount = 0;
  bool m_valid = false;
};

} // namespace GameMemory
