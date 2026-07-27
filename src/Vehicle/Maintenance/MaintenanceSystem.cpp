#include "MaintenanceSystem.h"

#include "../../Core/Config.h"
#include "../../../sdk/inc/natives.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace MaintenanceSystem {
namespace {

State s_state;
std::unordered_map<std::uint64_t, State> s_states;
std::uint64_t s_activeKey = 0;

std::uint64_t VehicleKey(Vehicle vehicle) {
  const std::uint64_t model =
      static_cast<std::uint32_t>(ENTITY::GET_ENTITY_MODEL(vehicle));
  const char *plate = VEHICLE::GET_VEHICLE_NUMBER_PLATE_TEXT(vehicle);
  std::uint64_t hash = 1469598103934665603ull;
  if (plate) {
    for (const unsigned char *p =
             reinterpret_cast<const unsigned char *>(plate);
         *p; ++p) {
      hash ^= *p;
      hash *= 1099511628211ull;
    }
  }
  return (model << 32) ^ hash;
}

} // namespace

void Reset() {
  s_state = State{};
  s_activeKey = 0;
}

void SelectVehicle(Vehicle vehicle) {
  if (!vehicle || !ENTITY::DOES_ENTITY_EXIST(vehicle))
    return;
  if (s_activeKey)
    s_states[s_activeKey] = s_state;
  s_activeKey = VehicleKey(vehicle);
  const auto it = s_states.find(s_activeKey);
  s_state = it != s_states.end() ? it->second : State{};
  s_states[s_activeKey] = s_state;
}

void Update(float rpm, float throttle, float speedKmH, float oilTemperature,
            bool engineOn) {
  if (!Config::MaintenanceEnabled)
    return;
  const float dt = std::clamp(MISC::GET_FRAME_TIME(), 0.001f, 0.05f);
  const float distance = (std::max)(0.0f, speedKmH) * dt / 3600.0f;
  s_state.odometerKm += distance;
  if (engineOn)
    s_state.engineHours += dt / 3600.0f;

  const float coldPenalty =
      oilTemperature < 0.30f ? (0.30f - oilTemperature) * 1.8f : 0.0f;
  const float heatPenalty =
      oilTemperature > 0.82f ? (oilTemperature - 0.82f) * 8.0f : 0.0f;
  const float load = std::clamp(rpm * 0.45f + throttle * 0.55f, 0.0f, 1.0f);
  const float wear =
      distance * (0.00035f + load * 0.00030f) +
      (engineOn ? dt * 0.0000009f * (1.0f + coldPenalty + heatPenalty)
                : 0.0f);
  s_state.oilLife = (std::max)(
      0.0f, s_state.oilLife - wear * Config::OilWearMultiplier);
  s_state.oilLevel =
      (std::max)(0.55f, 1.0f - (1.0f - s_state.oilLife) * 0.32f);
  s_state.serviceDue = s_state.oilLife < 0.18f;
  if (s_activeKey)
    s_states[s_activeKey] = s_state;
}

void ServiceOil() {
  s_state.oilLife = 1.0f;
  s_state.oilLevel = 1.0f;
  s_state.serviceDue = false;
  if (s_activeKey)
    s_states[s_activeKey] = s_state;
}

float GetPowerFactor() {
  if (!Config::MaintenanceEnabled)
    return 1.0f;
  if (s_state.oilLife >= 0.18f)
    return 1.0f;
  return std::clamp(0.88f + s_state.oilLife / 0.18f * 0.12f,
                    0.88f, 1.0f);
}

const State &GetState() { return s_state; }

} // namespace MaintenanceSystem
