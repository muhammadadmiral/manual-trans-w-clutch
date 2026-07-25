#pragma once

#include <cstdint>
#include <string>

// Defines the resolved memory offsets for CVehicle properties.
// These are absolute displacements from the CVehicle object's base address.
struct VehicleOffsets {
  uint32_t Gear = 0;
  uint32_t NextGear = 0;
  uint32_t TopGear = 0;
  uint32_t GearRatios = 0;
  uint32_t GearRatiosInline = 0;
  uint32_t Clutch = 0;
  uint32_t RPM = 0;
  uint32_t Throttle = 0;
  uint32_t ThrottlePedal = 0;
  uint32_t HandlingPtr = 0;
  uint32_t DriveInertia = 0;
  uint32_t DriveMaxFlatVel = 0;
  uint32_t DriveForce = 0;
  uint32_t WheelsPtr = 0;
  uint32_t WheelCount = 0;
  uint32_t WheelAngularVelocity = 0;
  uint32_t WheelLoad = 0;
  uint32_t WheelBrakePressure = 0;
  uint32_t WheelPower = 0;
  uint32_t FuelLevel = 0;
  uint32_t OilLevel = 0;
  uint32_t LightsBroken = 0;
  uint32_t LightsVisuallyBroken = 0;
  uint32_t VehicleFlags = 0;
  uint32_t HoverTransformRatioLerp = 0;

  bool
  IsCompleteCore() const; // Checks if essential transmission offsets are found
};

class OffsetResolver {
public:
  // Scans the GTA V process memory to find and resolve all known CVehicle
  // offsets. Returns true if at least the core transmission offsets (Gear,
  // NextGear, Clutch, RPM) were found.
  static bool ScanPatterns(VehicleOffsets &outOffsets,
                           std::string &outFailureReason);

  // Resolves non-core fields without requiring the Gear signature to match.
  // This is also used to enrich a known-good build-specific INI fallback.
  static void EnrichOptionalOffsets(VehicleOffsets &offsets);

private:
  static uint32_t ResolveRipDisplacement(uintptr_t instructionAddr,
                                         ptrdiff_t displacementOffset);
};
