#include "GameMemoryEngine.h"

#include "OffsetResolver.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace GameMemory {
namespace {

constexpr ULONGLONG kBindingRevalidateMs = 2000;

struct MemoryRegion {
  uintptr_t start = 0;
  uintptr_t end = 0;
  bool readable = false;
  bool writable = false;
};

struct VehicleBinding {
  uintptr_t address = 0;
  int identity = 0;
  VehicleOffsets offsets{};
  ULONGLONG validatedAt = 0;
  MemoryRegion vehicle{};
  uintptr_t handlingAddress = 0;
  MemoryRegion handling{};
  uintptr_t ratiosAddress = 0;
  MemoryRegion ratios{};
  std::array<uintptr_t, 16> wheelAddresses{};
  std::array<MemoryRegion, 16> wheelRegions{};
  uint8_t wheelCount = 0;
  bool valid = false;
};

VehicleBinding s_binding;

bool IsReadableProtection(DWORD protection) {
  if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
    return false;
  switch (protection & 0xFF) {
  case PAGE_READONLY:
  case PAGE_READWRITE:
  case PAGE_WRITECOPY:
  case PAGE_EXECUTE_READ:
  case PAGE_EXECUTE_READWRITE:
  case PAGE_EXECUTE_WRITECOPY:
    return true;
  default:
    return false;
  }
}

bool IsWritableProtection(DWORD protection) {
  if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
    return false;
  switch (protection & 0xFF) {
  case PAGE_READWRITE:
  case PAGE_WRITECOPY:
  case PAGE_EXECUTE_READWRITE:
  case PAGE_EXECUTE_WRITECOPY:
    return true;
  default:
    return false;
  }
}

MemoryRegion QueryRegion(uintptr_t address) {
  MemoryRegion result{};
  if (!address)
    return result;

  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(reinterpret_cast<const void *>(address), &info,
                    sizeof(info)) ||
      info.State != MEM_COMMIT || !IsReadableProtection(info.Protect)) {
    return result;
  }

  result.start = reinterpret_cast<uintptr_t>(info.BaseAddress);
  if (info.RegionSize >
      (std::numeric_limits<uintptr_t>::max)() - result.start) {
    return MemoryRegion{};
  }
  result.end = result.start + info.RegionSize;
  result.readable = true;
  result.writable = IsWritableProtection(info.Protect);
  return result;
}

bool Contains(const MemoryRegion &region, uintptr_t address, size_t size) {
  if (!region.readable || !address || !size || address < region.start ||
      address >= region.end) {
    return false;
  }
  return size <= region.end - address;
}

bool ResolveAddress(uintptr_t base, uint32_t offset, uintptr_t &result) {
  if (!base || offset > (std::numeric_limits<uintptr_t>::max)() - base)
    return false;
  result = base + offset;
  return true;
}

template <typename T>
T ReadValue(uintptr_t address) {
  T value{};
  std::memcpy(&value, reinterpret_cast<const void *>(address), sizeof(T));
  return value;
}

template <typename T>
void WriteValue(uintptr_t address, const T &value) {
  std::memcpy(reinterpret_cast<void *>(address), &value, sizeof(T));
}

bool SameLayout(const VehicleOffsets &left, const VehicleOffsets &right) {
  return std::memcmp(&left, &right, sizeof(VehicleOffsets)) == 0;
}

bool ContainsVehicleField(const VehicleBinding &binding, uint32_t offset,
                          size_t size) {
  uintptr_t address = 0;
  return offset != 0 && ResolveAddress(binding.address, offset, address) &&
         Contains(binding.vehicle, address, size);
}

void BindPointerRoots(VehicleBinding &binding) {
  const VehicleOffsets &offsets = binding.offsets;

  if (ContainsVehicleField(binding, offsets.HandlingPtr,
                           sizeof(uintptr_t))) {
    const uintptr_t pointerAddress =
        binding.address + offsets.HandlingPtr;
    binding.handlingAddress = ReadValue<uintptr_t>(pointerAddress);
    binding.handling = QueryRegion(binding.handlingAddress);
    if (!Contains(binding.handling, binding.handlingAddress, sizeof(float))) {
      binding.handlingAddress = 0;
      binding.handling = {};
    }
  }

  if (offsets.GearRatios != 0) {
    if (offsets.GearRatiosInline != 0) {
      uintptr_t ratios = 0;
      if (ResolveAddress(binding.address, offsets.GearRatios, ratios) &&
          Contains(binding.vehicle, ratios, sizeof(float) * 33)) {
        binding.ratiosAddress = ratios;
        binding.ratios = binding.vehicle;
      }
    } else if (ContainsVehicleField(binding, offsets.GearRatios,
                                    sizeof(uintptr_t))) {
      const uintptr_t pointerAddress =
          binding.address + offsets.GearRatios;
      const uintptr_t ratios = ReadValue<uintptr_t>(pointerAddress);
      const MemoryRegion region = QueryRegion(ratios);
      if (Contains(region, ratios, sizeof(float) * 33)) {
        binding.ratiosAddress = ratios;
        binding.ratios = region;
      }
    }
  }

  if (!ContainsVehicleField(binding, offsets.WheelsPtr, sizeof(uintptr_t)) ||
      !ContainsVehicleField(binding, offsets.WheelCount, sizeof(int))) {
    return;
  }

  const int rawCount =
      ReadValue<int>(binding.address + offsets.WheelCount);
  if (rawCount < 1 || rawCount > 16)
    return;

  const uintptr_t wheelArray =
      ReadValue<uintptr_t>(binding.address + offsets.WheelsPtr);
  const MemoryRegion arrayRegion = QueryRegion(wheelArray);
  if (!Contains(arrayRegion, wheelArray,
                static_cast<size_t>(rawCount) * sizeof(uintptr_t))) {
    return;
  }

  binding.wheelCount = static_cast<uint8_t>(rawCount);
  for (uint8_t index = 0; index < binding.wheelCount; ++index) {
    const uintptr_t wheel = ReadValue<uintptr_t>(
        wheelArray + static_cast<uintptr_t>(index) * sizeof(uintptr_t));
    const MemoryRegion region = QueryRegion(wheel);
    uintptr_t angularVelocity = 0;
    if (offsets.WheelAngularVelocity != 0 &&
        ResolveAddress(wheel, offsets.WheelAngularVelocity,
                       angularVelocity) &&
        Contains(region, angularVelocity, sizeof(float))) {
      binding.wheelAddresses[index] = wheel;
      binding.wheelRegions[index] = region;
    }
  }
}

void RefreshBinding(uintptr_t address, const VehicleOffsets *offsets,
                    int identity, ULONGLONG now) {
  s_binding = {};
  s_binding.address = address;
  s_binding.identity = identity;
  s_binding.validatedAt = now;
  if (!address || !offsets || !offsets->IsCompleteCore())
    return;

  s_binding.offsets = *offsets;
  s_binding.vehicle = QueryRegion(address);
  if (!s_binding.vehicle.readable)
    return;

  const bool coreReadable =
      ContainsVehicleField(s_binding, offsets->Gear, sizeof(uint8_t)) &&
      ContainsVehicleField(s_binding, offsets->NextGear, sizeof(uint8_t)) &&
      ContainsVehicleField(s_binding, offsets->RPM, sizeof(float)) &&
      ContainsVehicleField(s_binding, offsets->Clutch, sizeof(float));
  if (!coreReadable)
    return;

  s_binding.valid = true;
  BindPointerRoots(s_binding);
}

const VehicleBinding &GetBinding(uintptr_t address,
                                 const VehicleOffsets *offsets,
                                 int identity) {
  const ULONGLONG now = GetTickCount64();
  const bool cacheMatches =
      offsets && s_binding.address == address &&
      s_binding.identity == identity &&
      SameLayout(s_binding.offsets, *offsets);
  if (!cacheMatches || !s_binding.valid ||
      now - s_binding.validatedAt >= kBindingRevalidateMs) {
    RefreshBinding(address, offsets, identity, now);
  }
  return s_binding;
}

bool ContainsOffset(uintptr_t base, uintptr_t regionStart,
                    uintptr_t regionEnd, uint32_t offset, size_t size) {
  uintptr_t address = 0;
  if (!offset || !ResolveAddress(base, offset, address)) {
    return false;
  }
  const MemoryRegion region{regionStart, regionEnd, true, false};
  return Contains(region, address, size);
}

} // namespace

bool CHandlingData::CanRead(uint32_t offset, size_t size) const {
  return IsValid() &&
         ContainsOffset(m_address, m_regionStart, m_regionEnd, offset, size);
}

bool CHandlingData::CanWrite(uint32_t offset, size_t size) const {
  return m_writable && CanRead(offset, size);
}

float CHandlingData::GetDriveForce() const {
  return CanRead(m_offsets->DriveForce, sizeof(float))
             ? ReadValue<float>(m_address + m_offsets->DriveForce)
             : 0.0f;
}

float CHandlingData::GetDriveInertia() const {
  return CanRead(m_offsets->DriveInertia, sizeof(float))
             ? ReadValue<float>(m_address + m_offsets->DriveInertia)
             : 0.0f;
}

float CHandlingData::GetClutchChangeRateScaleUpShift() const {
  return CanRead(m_offsets->ClutchChangeRateScaleUpShift, sizeof(float))
             ? ReadValue<float>(
                   m_address + m_offsets->ClutchChangeRateScaleUpShift)
             : 0.0f;
}

float CHandlingData::GetClutchChangeRateScaleDownShift() const {
  return CanRead(m_offsets->ClutchChangeRateScaleDownShift, sizeof(float))
             ? ReadValue<float>(
                   m_address + m_offsets->ClutchChangeRateScaleDownShift)
             : 0.0f;
}

float CHandlingData::GetDriveMaxFlatVel() const {
  return CanRead(m_offsets->DriveMaxFlatVel, sizeof(float))
             ? ReadValue<float>(m_address + m_offsets->DriveMaxFlatVel)
             : 0.0f;
}

float CHandlingData::GetInitialDriveMaxFlatVel() const {
  return CanRead(m_offsets->InitialDriveMaxFlatVel, sizeof(float))
             ? ReadValue<float>(
                   m_address + m_offsets->InitialDriveMaxFlatVel)
             : 0.0f;
}

void CHandlingData::SetDriveForce(float force) {
  if (CanWrite(m_offsets->DriveForce, sizeof(float)))
    WriteValue(m_address + m_offsets->DriveForce, force);
}

CVehicle::CVehicle(uintptr_t address, const VehicleOffsets *offsets,
                   int identity)
    : m_address(address), m_offsets(offsets) {
  const VehicleBinding &binding = GetBinding(address, offsets, identity);
  if (binding.address != address || !binding.valid)
    return;

  m_vehicleRegionStart = binding.vehicle.start;
  m_vehicleRegionEnd = binding.vehicle.end;
  m_vehicleWritable = binding.vehicle.writable;
  m_handlingAddress = binding.handlingAddress;
  m_handlingRegionStart = binding.handling.start;
  m_handlingRegionEnd = binding.handling.end;
  m_handlingWritable = binding.handling.writable;
  m_ratiosAddress = binding.ratiosAddress;
  m_ratiosRegionStart = binding.ratios.start;
  m_ratiosRegionEnd = binding.ratios.end;
  m_ratiosWritable = binding.ratios.writable;
  m_wheelCount = binding.wheelCount;
  for (size_t index = 0; index < m_wheelAddresses.size(); ++index) {
    m_wheelAddresses[index] = binding.wheelAddresses[index];
    m_wheelRegionStarts[index] = binding.wheelRegions[index].start;
    m_wheelRegionEnds[index] = binding.wheelRegions[index].end;
  }
  m_valid = true;
}

bool CVehicle::CanReadVehicle(uint32_t offset, size_t size) const {
  return m_valid &&
         ContainsOffset(m_address, m_vehicleRegionStart, m_vehicleRegionEnd,
                        offset, size);
}

bool CVehicle::CanWriteVehicle(uint32_t offset, size_t size) const {
  return m_vehicleWritable && CanReadVehicle(offset, size);
}

CHandlingData CVehicle::GetHandlingData() const {
  if (!m_valid || !m_handlingAddress)
    return {};
  return CHandlingData(m_handlingAddress, m_offsets, m_handlingRegionStart,
                       m_handlingRegionEnd, m_handlingWritable);
}

uint8_t CVehicle::GetGear() const {
  return CanReadVehicle(m_offsets->Gear, sizeof(uint8_t))
             ? ReadValue<uint8_t>(m_address + m_offsets->Gear)
             : 0;
}

uint8_t CVehicle::GetNextGear() const {
  return CanReadVehicle(m_offsets->NextGear, sizeof(uint8_t))
             ? ReadValue<uint8_t>(m_address + m_offsets->NextGear)
             : 0;
}

uint8_t CVehicle::GetTopGear() const {
  return CanReadVehicle(m_offsets->TopGear, sizeof(uint8_t))
             ? ReadValue<uint8_t>(m_address + m_offsets->TopGear)
             : 0;
}

float CVehicle::GetGearRatio(uint8_t gearIndex) const {
  if (!m_valid || !m_ratiosAddress || gearIndex > 32)
    return 0.0f;
  const uintptr_t address =
      m_ratiosAddress + static_cast<uintptr_t>(gearIndex) * sizeof(float);
  const MemoryRegion region{
      m_ratiosRegionStart, m_ratiosRegionEnd, true, m_ratiosWritable};
  return Contains(region, address, sizeof(float))
             ? ReadValue<float>(address)
             : 0.0f;
}

bool CVehicle::CanWriteGearRatios() const {
  if (!m_valid || !m_ratiosAddress || !m_ratiosWritable ||
      m_offsets->GearRatiosInline == 0) {
    return false;
  }
  const MemoryRegion region{
      m_ratiosRegionStart, m_ratiosRegionEnd, true, m_ratiosWritable};
  return Contains(region, m_ratiosAddress, sizeof(float) * 17);
}

float CVehicle::GetClutch() const {
  return CanReadVehicle(m_offsets->Clutch, sizeof(float))
             ? ReadValue<float>(m_address + m_offsets->Clutch)
             : 0.0f;
}

float CVehicle::GetRPM() const {
  return CanReadVehicle(m_offsets->RPM, sizeof(float))
             ? ReadValue<float>(m_address + m_offsets->RPM)
             : 0.0f;
}

float CVehicle::GetThrottle() const {
  return CanReadVehicle(m_offsets->Throttle, sizeof(float))
             ? ReadValue<float>(m_address + m_offsets->Throttle)
             : 0.0f;
}

float CVehicle::GetThrottlePedal() const {
  return CanReadVehicle(m_offsets->ThrottlePedal, sizeof(float))
             ? ReadValue<float>(m_address + m_offsets->ThrottlePedal)
             : 0.0f;
}

float CVehicle::GetTransmissionDriveForce() const {
  return CanReadVehicle(m_offsets->TransmissionDriveForce, sizeof(float))
             ? ReadValue<float>(
                   m_address + m_offsets->TransmissionDriveForce)
             : 0.0f;
}

float CVehicle::GetTransmissionDriveMaxFlatVel() const {
  return CanReadVehicle(m_offsets->TransmissionDriveMaxFlatVel, sizeof(float))
             ? ReadValue<float>(
                   m_address + m_offsets->TransmissionDriveMaxFlatVel)
             : 0.0f;
}

uint8_t CVehicle::GetWheelCount() const {
  return m_valid ? m_wheelCount : 0;
}

WheelTelemetry CVehicle::GetWheelTelemetry(uint8_t index) const {
  WheelTelemetry result{};
  if (!m_valid || index >= m_wheelCount || !m_wheelAddresses[index] ||
      m_offsets->WheelAngularVelocity == 0) {
    return result;
  }

  const uintptr_t wheel = m_wheelAddresses[index];
  const MemoryRegion region{m_wheelRegionStarts[index],
                            m_wheelRegionEnds[index], true, false};
  uintptr_t address = 0;
  if (!ResolveAddress(wheel, m_offsets->WheelAngularVelocity, address) ||
      !Contains(region, address, sizeof(float))) {
    return result;
  }
  result.angularVelocity = ReadValue<float>(address);

  if (m_offsets->WheelLoad != 0 &&
      ResolveAddress(wheel, m_offsets->WheelLoad, address) &&
      Contains(region, address, sizeof(float))) {
    result.load = ReadValue<float>(address);
  }
  if (m_offsets->WheelBrakePressure != 0 &&
      ResolveAddress(wheel, m_offsets->WheelBrakePressure, address) &&
      Contains(region, address, sizeof(float))) {
    result.brakePressure = ReadValue<float>(address);
  }
  if (m_offsets->WheelPower != 0 &&
      ResolveAddress(wheel, m_offsets->WheelPower, address) &&
      Contains(region, address, sizeof(float))) {
    result.power = ReadValue<float>(address);
  }
  result.valid = true;
  return result;
}

float CVehicle::GetFuelLevel() const {
  return CanReadVehicle(m_offsets->FuelLevel, sizeof(float))
             ? ReadValue<float>(m_address + m_offsets->FuelLevel)
             : 0.0f;
}

float CVehicle::GetHoverTransformRatioLerp() const {
  return CanReadVehicle(m_offsets->HoverTransformRatioLerp, sizeof(float))
             ? ReadValue<float>(
                   m_address + m_offsets->HoverTransformRatioLerp)
             : 0.0f;
}

uint8_t CVehicle::GetLightsBroken() const {
  return CanReadVehicle(m_offsets->LightsBroken, sizeof(uint8_t))
             ? ReadValue<uint8_t>(m_address + m_offsets->LightsBroken)
             : 0;
}

uint8_t CVehicle::GetLightsVisuallyBroken() const {
  return CanReadVehicle(m_offsets->LightsVisuallyBroken, sizeof(uint8_t))
             ? ReadValue<uint8_t>(
                   m_address + m_offsets->LightsVisuallyBroken)
             : 0;
}

void CVehicle::SetGear(uint8_t gear) {
  if (CanWriteVehicle(m_offsets->Gear, sizeof(gear)))
    WriteValue(m_address + m_offsets->Gear, gear);
}

void CVehicle::SetNextGear(uint8_t gear) {
  if (CanWriteVehicle(m_offsets->NextGear, sizeof(gear)))
    WriteValue(m_address + m_offsets->NextGear, gear);
}

void CVehicle::SetTopGear(uint8_t gear) {
  if (CanWriteVehicle(m_offsets->TopGear, sizeof(gear)))
    WriteValue(m_address + m_offsets->TopGear, gear);
}

bool CVehicle::SetGearRatio(uint8_t gearIndex, float ratio) {
  if (!CanWriteGearRatios() || gearIndex > 16 || !std::isfinite(ratio))
    return false;
  const uintptr_t address =
      m_ratiosAddress + static_cast<uintptr_t>(gearIndex) * sizeof(float);
  const MemoryRegion region{
      m_ratiosRegionStart, m_ratiosRegionEnd, true, m_ratiosWritable};
  if (!Contains(region, address, sizeof(float)))
    return false;
  WriteValue(address, ratio);
  return true;
}

void CVehicle::SetClutch(float clutch) {
  if (CanWriteVehicle(m_offsets->Clutch, sizeof(clutch)))
    WriteValue(m_address + m_offsets->Clutch, clutch);
}

void CVehicle::SetRPM(float rpm) {
  if (CanWriteVehicle(m_offsets->RPM, sizeof(rpm)))
    WriteValue(m_address + m_offsets->RPM, rpm);
}

void CVehicle::SetThrottle(float throttle) {
  if (CanWriteVehicle(m_offsets->Throttle, sizeof(throttle)))
    WriteValue(m_address + m_offsets->Throttle, throttle);
}

void CVehicle::SetThrottlePedal(float throttle) {
  if (CanWriteVehicle(m_offsets->ThrottlePedal, sizeof(throttle)))
    WriteValue(m_address + m_offsets->ThrottlePedal, throttle);
}

void CVehicle::SetLightsBroken(uint8_t state) {
  if (CanWriteVehicle(m_offsets->LightsBroken, sizeof(state)))
    WriteValue(m_address + m_offsets->LightsBroken, state);
}

void CVehicle::SetLightsVisuallyBroken(uint8_t state) {
  if (CanWriteVehicle(m_offsets->LightsVisuallyBroken, sizeof(state)))
    WriteValue(m_address + m_offsets->LightsVisuallyBroken, state);
}

} // namespace GameMemory
