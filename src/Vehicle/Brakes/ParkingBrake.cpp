// =============================================================================
// ParkingBrake.cpp  —  Persistent handbrake + hill-hold
// =============================================================================
#include "ParkingBrake.h"
#include "../VehicleData.h"
#include "../../../sdk/inc/natives.h"
#include <Windows.h>

// Key default (0x50 = 'P'). Updated at runtime by ParkingBrake::SetKey().
static int s_parkingBrakeKey = 0x50;

namespace ParkingBrake {

static ParkingBrakeState s_state;

void SetKey(int vk) { s_parkingBrakeKey = vk; }
int  GetKey()       { return s_parkingBrakeKey; }

void Reset() {
  s_state = ParkingBrakeState{};
}

bool Update(Vehicle vehicle, VehicleData &data, float speedKmH,
            float throttle, int manualGear, bool isEngineOn) {

  const bool keyDown    = (GetAsyncKeyState(s_parkingBrakeKey) & 0x8000) != 0;
  const bool justPressed = keyDown && !s_state.wasKeyDown;
  s_state.wasKeyDown    = keyDown;

  if (justPressed) {
    if (s_state.isEngaged) {
      // Disengage only if throttle is applied or car is moving
      if (throttle > 0.05f || speedKmH > 0.5f) {
        s_state.isEngaged      = false;
        s_state.hillHoldActive = false;
        s_state.engageDelay    = 0;
        VEHICLE::SET_VEHICLE_HANDBRAKE(vehicle, FALSE);

        HUD::BEGIN_TEXT_COMMAND_THEFEED_POST("STRING");
        HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME("~g~Parking Brake Released");
        HUD::END_TEXT_COMMAND_THEFEED_POST_TICKER(false, false);
      }
    } else {
      s_state.isEngaged   = true;
      s_state.engageDelay = kEngageDelayFrames;

      HUD::BEGIN_TEXT_COMMAND_THEFEED_POST("STRING");
      HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME("~o~Parking Brake Engaged");
      HUD::END_TEXT_COMMAND_THEFEED_POST_TICKER(false, false);
    }
  }

  if (s_state.engageDelay > 0) --s_state.engageDelay;

  // Auto-release if moving fast enough with throttle
  if (s_state.isEngaged && speedKmH > kAutoReleaseSpeedKmH && throttle > 0.3f) {
    s_state.isEngaged = false;
    VEHICLE::SET_VEHICLE_HANDBRAKE(vehicle, FALSE);
    HUD::BEGIN_TEXT_COMMAND_THEFEED_POST("STRING");
    HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME("~r~Parking Brake Auto-Released");
    HUD::END_TEXT_COMMAND_THEFEED_POST_TICKER(false, false);
  }

  // Hill Hold: neutral + stopped + no throttle, rolling detected
  if (!s_state.isEngaged && isEngineOn && manualGear == 0 && speedKmH < 1.0f) {
    float speedDelta = speedKmH - s_state.lastSpeed;
    if (speedDelta > 0.1f && throttle < 0.05f) {
      s_state.hillHoldActive = true;
      PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 76, 1.0f);
    } else {
      s_state.hillHoldActive = false;
    }
  } else {
    s_state.hillHoldActive = false;
  }
  s_state.lastSpeed = speedKmH;

  // Inject handbrake every frame while engaged
  if (s_state.isEngaged && s_state.engageDelay == 0) {
    PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 76, kHandbrakeForce);
  }

  return s_state.isEngaged || s_state.hillHoldActive;
}

void ForceRelease(Vehicle vehicle) {
  s_state.isEngaged      = false;
  s_state.hillHoldActive = false;
  VEHICLE::SET_VEHICLE_HANDBRAKE(vehicle, FALSE);
}

bool IsEngaged()        { return s_state.isEngaged;     }
bool IsHillHoldActive() { return s_state.hillHoldActive; }

} // namespace ParkingBrake
