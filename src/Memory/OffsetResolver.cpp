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

  // 1. Core Transmission (Gear / NextGear / TopGear / GearRatios)
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
      offsets.GearRatios = baseGearOffset + 8;
    } else {
      outFailureReason = "Failed to read Gear RIP displacement.";
      return false;
    }
  } else {
    outFailureReason = "Could not find Gear pattern.";
    return false;
  }

  // 2. Core Engine (RPM / Clutch)
  addr = AOBScanner::FindUnique("F6 83 ? ? ? ? 07 75 ? 44 0F");
  if (addr != 0) {
    if (TryReadU32(addr - 42, offsets.RPM)) {
      offsets.Clutch = offsets.RPM + 12;
    } else {
      outFailureReason = "Failed to read RPM displacement.";
      return false;
    }
  } else {
    outFailureReason = "Could not find RPM pattern.";
    return false;
  }

  // 3. Drive Force
  // "F3 0F 10 8F A4 08 00 00 F3 0F 5E F0 41 0F 2F CA"
  addr = AOBScanner::FindUnique("F3 0F 10 8F ? ? ? ? F3 0F 5E F0 41 0F 2F CA");
  if (addr != 0) {
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
