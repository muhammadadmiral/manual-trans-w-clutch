#include "LightsLogic.h"

namespace LightsLogic {

void Update(Vehicle vehicle, VehicleData &data, int manualGear,
            float brakeInput, float throttleInput) {
  // 1. Brake Lights
  // The game automatically handles brake lights when pressing S, but ONLY if
  // the car is moving forward. If the car is stopped, pressing S turns on
  // Reverse lights. We force brake lights on if the user is pressing the brake
  // pedal, regardless of speed.
  if (brakeInput > 0.1f) {
    VEHICLE::SET_VEHICLE_BRAKE_LIGHTS(vehicle, TRUE);
  } else {
    // Let the game handle it, or turn them off if we are overriding
    // Actually, setting it to FALSE forces them off even when we want them
    // naturally on? Often it's better to only force TRUE when needed.
  }

  // 2. Reverse Lights Masking
  // The game automatically turns on Reverse Lights when Gear == 0.
  // However, it ALSO turns them on if the player presses S while stopped in
  // forward gears. We completely suppress fake reverse lights by artificially
  // "breaking" the reverse bulb in memory when we are not in reverse, and
  // "repairing" it when we are.

  // In GTA V, the reverse bulb is bit 1 (0x02) of the LightsBroken byte.
  // "F6 87 ? ? ? ? 02 75 06 C6 45 80 01" -> test byte ptr [rdi+offset], 2
  constexpr uint8_t kReverseBulbBit = 0x02;

  uint8_t currentBrokenState = data.GetLightsBroken();
  uint8_t currentVisBrokenState = data.GetLightsVisuallyBroken();

  if (manualGear != -1) {
    // We are NOT in reverse. The reverse bulb should be BROKEN so the game
    // cannot turn it on.
    if ((currentBrokenState & kReverseBulbBit) == 0) {
      data.SetLightsBroken(currentBrokenState | kReverseBulbBit);
    }
    if ((currentVisBrokenState & kReverseBulbBit) == 0) {
      data.SetLightsVisuallyBroken(currentVisBrokenState | kReverseBulbBit);
    }
  } else {
    // We ARE in reverse. The reverse bulb must be REPAIRED so it can turn on
    // naturally.
    if ((currentBrokenState & kReverseBulbBit) != 0) {
      data.SetLightsBroken(currentBrokenState & ~kReverseBulbBit);
    }
    if ((currentVisBrokenState & kReverseBulbBit) != 0) {
      data.SetLightsVisuallyBroken(currentVisBrokenState & ~kReverseBulbBit);
    }
  }
}

} // namespace LightsLogic
