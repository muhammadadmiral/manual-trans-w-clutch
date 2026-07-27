#include "ServiceInteraction.h"

#include "MaintenanceSystem.h"
#include "../../Core/Config.h"
#include "../../Core/ModLogger.h"
#include "../../Core/Renderer.h"
#include "../../../sdk/inc/natives.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>

namespace ServiceInteraction {
namespace {

Vehicle s_vehicle = 0;
bool s_active = false;
bool s_animStarted = false;
ULONGLONG s_startedAt = 0;
constexpr const char *kAnimDict =
    "amb@world_human_vehicle_mechanic@male@base";
constexpr const char *kAnimName = "base";
constexpr float kServiceSeconds = 8.0f;

float Distance(Vector3 a, Vector3 b) {
  const float x = a.x - b.x;
  const float y = a.y - b.y;
  const float z = a.z - b.z;
  return std::sqrt(x * x + y * y + z * z);
}

void Stop(Ped player) {
  if (s_active && player)
    TASK::CLEAR_PED_TASKS(player);
  STREAMING::REMOVE_ANIM_DICT(kAnimDict);
  s_active = false;
  s_animStarted = false;
  s_startedAt = 0;
}

} // namespace

void TrackVehicle(Vehicle vehicle) {
  if (vehicle && ENTITY::DOES_ENTITY_EXIST(vehicle))
    s_vehicle = vehicle;
}

void Update(Ped player) {
  if (!Config::MaintenanceEnabled || !player ||
      !ENTITY::DOES_ENTITY_EXIST(player) || PED::IS_PED_INJURED(player)) {
    Stop(player);
    return;
  }
  if (PED::IS_PED_IN_ANY_VEHICLE(player, FALSE)) {
    if (s_active)
      Stop(player);
    const Vehicle current = PED::GET_VEHICLE_PED_IS_USING(player);
    if (current && VEHICLE::GET_PED_IN_VEHICLE_SEAT(current, -1, FALSE) ==
                       player)
      TrackVehicle(current);
    return;
  }
  if (!s_vehicle || !ENTITY::DOES_ENTITY_EXIST(s_vehicle)) {
    Stop(player);
    return;
  }

  const Vector3 playerPos = ENTITY::GET_ENTITY_COORDS(player, TRUE);
  const Vector3 hoodPos =
      ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(s_vehicle, 0.0f, 2.0f,
                                                      0.0f);
  const bool nearHood = Distance(playerPos, hoodPos) < 2.4f;
  const bool engineRunning =
      VEHICLE::GET_IS_VEHICLE_ENGINE_RUNNING(s_vehicle) != FALSE;
  const bool keyDown =
      (GetAsyncKeyState(Config::KeyOilService) & 0x8000) != 0;

  if (!nearHood) {
    if (s_active)
      Stop(player);
    return;
  }

  if (!s_active) {
    if (engineRunning) {
      Renderer::DrawInteractionPanel(
          "OIL SERVICE", "Matikan mesin sebelum buka kap", -1.0f);
      return;
    }
    Renderer::DrawInteractionPanel(
        "OIL SERVICE", "Tahan tombol servis di dekat kap mesin",
        MaintenanceSystem::GetState().oilLife);
    if (!keyDown)
      return;
    STREAMING::REQUEST_ANIM_DICT(kAnimDict);
    TASK::TASK_TURN_PED_TO_FACE_ENTITY(player, s_vehicle, 500);
    s_active = true;
    s_animStarted = false;
    s_startedAt = GetTickCount64();
    LOG_INFO(Fuel, "Oil service started vehicle=%d", s_vehicle);
  }

  if (!keyDown || engineRunning || !nearHood) {
    Stop(player);
    return;
  }

  STREAMING::REQUEST_ANIM_DICT(kAnimDict);
  if (!s_animStarted && STREAMING::HAS_ANIM_DICT_LOADED(kAnimDict)) {
    TASK::TASK_PLAY_ANIM(player, kAnimDict, kAnimName, 2.0f, -2.0f,
                         -1, 49, 0.0f, FALSE, FALSE, FALSE);
    s_animStarted = true;
  }
  const float progress = std::clamp(
      static_cast<float>(GetTickCount64() - s_startedAt) /
          (kServiceSeconds * 1000.0f),
      0.0f, 1.0f);
  Renderer::DrawInteractionPanel("OIL SERVICE", "Mengganti oli...", progress);
  if (progress >= 1.0f) {
    MaintenanceSystem::ServiceOil();
    Stop(player);
    Renderer::ShowNotification("~g~Servis oli selesai");
    LOG_INFO(Fuel, "Oil service completed vehicle=%d", s_vehicle);
  }
}

bool IsActive() { return s_active; }

} // namespace ServiceInteraction
