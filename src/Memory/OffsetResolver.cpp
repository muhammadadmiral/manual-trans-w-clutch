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

static bool ResolveConsistentDisplacement(const char *pattern,
                                          ptrdiff_t displacementOffset,
                                          uint32_t &value) {
  const auto matches = AOBScanner::FindAll(pattern, 8);
  if (matches.empty())
    return false;

  uint32_t resolved = 0;
  for (const uintptr_t match : matches) {
    if (displacementOffset < 0 &&
        match < static_cast<uintptr_t>(-displacementOffset))
      return false;
    const uintptr_t address =
        displacementOffset >= 0
            ? match + static_cast<uintptr_t>(displacementOffset)
            : match - static_cast<uintptr_t>(-displacementOffset);
    uint32_t candidate = 0;
    if (!TryReadU32(address, candidate) || candidate == 0)
      return false;
    if (resolved != 0 && candidate != resolved)
      return false;
    resolved = candidate;
  }
  value = resolved;
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
      "F3 45 0F 10 8F ? ? ? ? 4C 89 F9");
  if (addr != 0 && TryReadU32(addr + 5, offsets.RPM)) {
    offsets.Clutch = offsets.RPM + 0xC;
    offsets.Throttle = offsets.RPM + 0x10;
  } else {
    addr = AOBScanner::FindUnique(
        "76 03 0F 28 F0 F3 44 0F 10 93 ? ? ? ?");
    if (addr != 0 && TryReadU32(addr + 10, offsets.RPM)) {
      offsets.Clutch = offsets.RPM + 0xC;
      offsets.Throttle = offsets.RPM + 0x10;
    } else {
      // Fallback lama buat build yang surrounding code-nya beda.
      addr = AOBScanner::FindUnique("F6 83 ? ? ? ? 07 75 ? 44 0F");
      if (addr == 0 || addr < 42 ||
          !TryReadU32(addr - 42, offsets.RPM)) {
        outFailureReason = "Could not resolve the RPM pattern.";
        return false;
      }
      offsets.Clutch = offsets.RPM + 0xC;
      offsets.Throttle = offsets.RPM + 0x10;
    }
  }

  // 2. Core Transmission (Gear / NextGear / TopGear / GearRatios)
  uint32_t baseGearOffset = 0;
  const bool enhancedGearPattern = ResolveConsistentDisplacement(
      "85 FF 41 0F 95 C0 48 89 F1 48 81 C1 ? ? ? ?", 12,
      baseGearOffset);
  addr = 0;
  if (!enhancedGearPattern)
    addr = AOBScanner::FindUnique(
        "48 8D 8F ? ? ? ? 4C 8B C3 F3 0F 11 7C 24");
  if (addr == 0)
    addr = AOBScanner::FindUnique(
        "88 8F ? ? ? ? 8B D1 48 8B 01 FF 90");
  if (addr == 0)
    addr = AOBScanner::FindUnique("88 8F ? ? ? ? 44 0F B6 C6");
  
  if (enhancedGearPattern || addr != 0) {
    if (!enhancedGearPattern) {
      baseGearOffset = ResolveRipDisplacement(addr, 3);
    }
    if (baseGearOffset == 0 && !enhancedGearPattern) {
      baseGearOffset = ResolveRipDisplacement(addr, 2);
    }
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

  // 3. Vehicle Flags
  addr = AOBScanner::FindUnique(
      "74 ? F6 80 ? ? ? ? ? 74 ? 80 BE");
  const bool enhancedVehicleFlags = addr != 0;
  if (addr == 0)
    addr = AOBScanner::FindUnique(
        "48 85 C0 74 3C 8B 80 ? ? ? ? C1 E8 0F");
  if (addr != 0) {
    offsets.VehicleFlags =
        ResolveRipDisplacement(addr, enhancedVehicleFlags ? 4 : 7);
  }

  // 4. Fuel / Oil Level
  uint32_t fuelOffset = 0;
  addr = AOBScanner::FindUnique(
      "66 83 F8 ? 74 ? F3 0F 10 86 ? ? ? ? 0F 57 C9");
  if (addr != 0)
    fuelOffset = ResolveRipDisplacement(addr, 10);
  if (fuelOffset == 0) {
    addr = AOBScanner::FindUnique("74 26 0F 57 C9");
    if (addr != 0)
      fuelOffset = ResolveRipDisplacement(addr, 8);
  }
  offsets.FuelLevel = fuelOffset;

  uint32_t oilOffset = 0;
  addr = AOBScanner::FindUnique(
      "74 ? F3 0F 10 81 ? ? ? ? 0F 57 C9 0F 2E C1 76");
  if (addr != 0)
    oilOffset = ResolveRipDisplacement(addr, 6);
  offsets.OilLevel =
      oilOffset != 0 ? oilOffset : (fuelOffset != 0 ? fuelOffset + 4 : 0);

  // 5. Lights Broken (Reverse/Brake indicators)
  // "F6 87 ? ? ? ? 02 75 06 C6 45 80 01"
  addr = AOBScanner::FindUnique("F6 87 ? ? ? ? 02 75 06 C6 45 80 01");
  if (addr != 0) {
    offsets.LightsBroken = ResolveRipDisplacement(addr, 2);
    if (offsets.LightsBroken != 0) {
      offsets.LightsVisuallyBroken = offsets.LightsBroken + 8;
    }
  }

  // 6. Hover Transform (Deluxo)
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
  if (offsets.NextGear >= 0x68) {
    // Field -0x68 sempat terlihat seperti drive-force saat dibaca, tetapi
    // write ke sana merusak state internal Enhanced. Jangan pernah expose.
    offsets.TransmissionDriveForce = 0;
    offsets.TransmissionDriveMaxFlatVel = offsets.NextGear - 0x60;
  }

  // CVehicle::m_handlingData. The handling fields below are offsets inside
  // CHandlingData, not displacements from CVehicle.
  bool enhancedHandlingPattern = false;
  uintptr_t addr = AOBScanner::FindUnique(
      "48 83 C0 ? 48 8B 8B ? ? ? ? F3 0F 10 89");
  if (addr != 0)
    enhancedHandlingPattern = true;
  if (addr == 0)
    addr = AOBScanner::FindUnique(
        "3C 03 0F 85 ? ? ? ? 48 8B 41 20 48 8B 88");
  if (addr != 0) {
    offsets.HandlingPtr =
        ResolveRipDisplacement(addr, enhancedHandlingPattern ? 7 : 0x16);
  }
  if (offsets.HandlingPtr != 0) {
    // Stable CHandlingData layout. This also enriches an older INI that
    // already has a validated handling pointer but predates these fields.
    offsets.DriveInertia = 0x54;
    offsets.ClutchChangeRateScaleUpShift = 0x58;
    offsets.ClutchChangeRateScaleDownShift = 0x5C;
    offsets.DriveForce = 0x60;
    offsets.DriveMaxFlatVel = 0x64;
    offsets.InitialDriveMaxFlatVel = 0x68;
  }

  // fThrottleP shares the group following steering input in CVehicle.
  bool enhancedPedalPattern = false;
  addr = AOBScanner::FindUnique(
      "0F 56 F9 F3 0F 11 BE ? ? ? ? 48 8B 86");
  if (addr != 0)
    enhancedPedalPattern = true;
  if (addr == 0)
    addr = AOBScanner::FindUnique(
        "74 0A F3 0F 11 B3 ? ? ? ? EB 25");
  if (addr != 0) {
    const uint32_t steeringInput =
        ResolveRipDisplacement(addr, enhancedPedalPattern ? 7 : 6);
    if (steeringInput != 0)
      offsets.ThrottlePedal = steeringInput + 0x10;
  }

  uint32_t enhancedWheelCount = 0;
  if (ResolveConsistentDisplacement(
          "4C 8B 89 ? ? ? ? 45 31 D2 0F 1F", -9,
          enhancedWheelCount)) {
    offsets.WheelCount = enhancedWheelCount;
    if (offsets.WheelCount >= 8)
      offsets.WheelsPtr = offsets.WheelCount - 8;
  } else {
    addr = AOBScanner::FindUnique("3B B7 ? ? ? ? 7D 0D");
    if (addr != 0) {
      offsets.WheelCount = ResolveRipDisplacement(addr, 2);
      if (offsets.WheelCount >= 8)
        offsets.WheelsPtr = offsets.WheelCount - 8;
    }
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
