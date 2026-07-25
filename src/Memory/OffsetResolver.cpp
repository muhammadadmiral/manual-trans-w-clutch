#include "OffsetResolver.h"
#include "AOBScanner.h"

#define NOMINMAX
#include <Windows.h>
#include <cstring>

bool VehicleOffsets::IsCompleteCore() const {
  return Gear != 0 && NextGear != 0 && Clutch != 0 && RPM != 0;
}

static bool TryReadU32(uintptr_t address, uint32_t &value) {
  if (!AOBScanner::IsReadable(address, sizeof(value)))
    return false;
  std::memcpy(&value, reinterpret_cast<const void *>(address), sizeof(value));
  return true;
}

uint32_t OffsetResolver::ResolveRipDisplacement(uintptr_t instructionAddr,
                                                ptrdiff_t displacementOffset) {
  if (instructionAddr == 0)
    return 0;
  uint32_t displacement = 0;
  if (TryReadU32(instructionAddr + displacementOffset, displacement)) {
    return displacement;
  }
  return 0;
}

bool OffsetResolver::ScanPatterns(VehicleOffsets &outOffsets,
                                  std::string &outFailureReason) {
  VehicleOffsets offsets{};
  uintptr_t addr = 0;

  // Tiga field ini satu cluster di CVehicle. Throttle di sini adalah state
  // mesin/audio, beda dengan pedal input GTA.
  addr = AOBScanner::FindUnique(
      "76 03 0F 28 F0 F3 44 0F 10 93 ? ? ? ?");
  if (addr != 0 && TryReadU32(addr + 10, offsets.RPM)) {
    offsets.Clutch = offsets.RPM + 0xC;
    offsets.Throttle = offsets.RPM + 0x10;
  } else {
    // Older fallback retained for builds whose surrounding code changed.
    addr = AOBScanner::FindUnique("F6 83 ? ? ? ? 07 75 ? 44 0F");
    if (addr == 0 || !TryReadU32(addr - 42, offsets.RPM)) {
      outFailureReason = "Could not resolve the RPM pattern.";
      return false;
    }
    offsets.Clutch = offsets.RPM + 0xC;
    offsets.Throttle = offsets.RPM + 0x10;
  }

  // 2. Core Transmission (Gear / NextGear / TopGear / GearRatios)
  addr = AOBScanner::FindUnique("48 8D 8F ? ? ? ? 4C 8B C3 F3 0F 11 7C 24");
  if (addr == 0) addr = AOBScanner::FindUnique("88 8F ? ? ? ? 8B D1 48 8B 01 FF 90");
  if (addr == 0) addr = AOBScanner::FindUnique("88 8F ? ? ? ? 44 0F B6 C6");
  
  if (addr != 0) {
    uint32_t baseGearOffset = ResolveRipDisplacement(addr, 3);
    if (baseGearOffset == 0) baseGearOffset = ResolveRipDisplacement(addr, 2); // For newer 88 8F pattern
    if (baseGearOffset != 0) {
      offsets.NextGear = baseGearOffset;
      offsets.Gear = baseGearOffset + 2;
      offsets.TopGear = baseGearOffset + 6;
      offsets.GearRatios = baseGearOffset + 0xC;
      offsets.GearRatiosInline = 1;
    } else {
      outFailureReason = "Failed to read Gear RIP displacement.";
      return false;
    }
  } else {
    outFailureReason = "Could not find Gear pattern.";
    return false;
  }

  EnrichOptionalOffsets(offsets);

  // 3. Drive Force
  // "F3 0F 10 8F A4 08 00 00 F3 0F 5E F0 41 0F 2F CA"
  addr = AOBScanner::FindUnique("F3 0F 10 8F ? ? ? ? F3 0F 5E F0 41 0F 2F CA");
  if (addr != 0 && offsets.HandlingPtr == 0) {
    offsets.DriveForce = ResolveRipDisplacement(addr, 4);
  }

  // 4. Vehicle Flags
  // "48 85 C0 74 3C 8B 80 ? ? ? ? C1 E8 0F"
  addr = AOBScanner::FindUnique("48 85 C0 74 3C 8B 80 ? ? ? ? C1 E8 0F");
  if (addr != 0) {
    offsets.VehicleFlags = ResolveRipDisplacement(addr, 7);
  }

  // 5. Fuel / Oil Level
  // "74 26 0F 57 C9"
  addr = AOBScanner::FindUnique("74 26 0F 57 C9");
  if (addr != 0) {
    uint32_t fuelOffset = ResolveRipDisplacement(addr, 8);
    if (fuelOffset != 0) {
      offsets.FuelLevel = fuelOffset;
      offsets.OilLevel = fuelOffset + 4;
    }
  }

  // 6. Lights Broken (Reverse/Brake indicators)
  // "F6 87 ? ? ? ? 02 75 06 C6 45 80 01"
  addr = AOBScanner::FindUnique("F6 87 ? ? ? ? 02 75 06 C6 45 80 01");
  if (addr != 0) {
    offsets.LightsBroken = ResolveRipDisplacement(addr, 2);
    if (offsets.LightsBroken != 0) {
      offsets.LightsVisuallyBroken = offsets.LightsBroken + 8;
    }
  }

  // 7. Hover Transform (Deluxo)
  addr = AOBScanner::FindUnique("F3 0F 11 B3 ? ? ? ? 44 88 ? ? ? ? ? 48 85 C9");
  if (addr != 0) {
    uint32_t hoverBase = ResolveRipDisplacement(addr, 4);
    if (hoverBase != 0) {
      offsets.HoverTransformRatioLerp = hoverBase + 0x28;
    }
  }

  outOffsets = offsets;
  return true;
}

void OffsetResolver::EnrichOptionalOffsets(VehicleOffsets &offsets) {
  if (offsets.RPM != 0) {
    offsets.Clutch = offsets.RPM + 0xC;
    offsets.Throttle = offsets.RPM + 0x10;
  }

  // CVehicle::m_handlingData. The handling fields below are offsets inside
  // CHandlingData, not displacements from CVehicle.
  uintptr_t addr = AOBScanner::FindUnique(
      "3C 03 0F 85 ? ? ? ? 48 8B 41 20 48 8B 88");
  if (addr != 0) {
    offsets.HandlingPtr = ResolveRipDisplacement(addr, 0x16);
    if (offsets.HandlingPtr != 0) {
      offsets.DriveInertia = 0x54;
      offsets.DriveForce = 0x60;
      offsets.DriveMaxFlatVel = 0x64;
    }
  }

  // fThrottleP shares the group following steering input in CVehicle.
  addr = AOBScanner::FindUnique(
      "74 0A F3 0F 11 B3 ? ? ? ? EB 25");
  if (addr != 0) {
    const uint32_t steeringInput = ResolveRipDisplacement(addr, 6);
    if (steeringInput != 0)
      offsets.ThrottlePedal = steeringInput + 0x10;
  }

  addr = AOBScanner::FindUnique("3B B7 ? ? ? ? 7D 0D");
  if (addr != 0) {
    offsets.WheelCount = ResolveRipDisplacement(addr, 2);
    if (offsets.WheelCount >= 8)
      offsets.WheelsPtr = offsets.WheelCount - 8;
  }

  addr = AOBScanner::FindUnique(
      "45 0F 57 C9 F3 0F 11 83 ? ? ? ? F3 0F 5C");
  if (addr != 0) {
    const uint32_t suspensionCompression =
        ResolveRipDisplacement(addr, 8);
    if (suspensionCompression != 0)
      offsets.WheelAngularVelocity = suspensionCompression + 0xC;
  }

  addr = AOBScanner::FindUnique(
      "0F 2F 81 ? ? ? ? 0F 97 C0 EB ? D1 ?");
  if (addr != 0) {
    const uint32_t steeringAngle = ResolveRipDisplacement(addr, 3);
    if (steeringAngle != 0) {
      offsets.WheelLoad = steeringAngle - 0x10;
      offsets.WheelBrakePressure = steeringAngle + 0x4;
      offsets.WheelPower = steeringAngle + 0x8;
    }
  }
}
