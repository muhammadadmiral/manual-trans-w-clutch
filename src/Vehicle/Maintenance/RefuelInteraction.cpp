#include "RefuelInteraction.h"

#include "../../Core/Config.h"
#include "../../Core/ModLogger.h"
#include "../../Core/Renderer.h"
#include "../Engine/FuelSystem.h"
#include "../../../sdk/inc/natives.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace RefuelInteraction {
namespace {

enum class Phase {
  Idle,
  Loading,
  Refueling
};

Phase s_phase = Phase::Idle;
Vehicle s_vehicle = 0;
Object s_prop = 0;
Object s_pump = 0;
ULONGLONG s_phaseTick = 0;
bool s_usingJerryCan = false;
bool s_promptVisible = false;
std::vector<Blip> s_stationBlips;

struct Station {
  float x;
  float y;
  float z;
};

constexpr std::array<Station, 24> kStations = {{
    {49.42f, 2778.79f, 58.04f},
    {263.89f, -1261.31f, 29.29f},
    {1039.96f, 2671.13f, 39.55f},
    {1207.26f, 2660.18f, 37.90f},
    {2539.69f, 2594.19f, 37.94f},
    {2679.86f, 3263.95f, 55.24f},
    {2005.06f, 3773.89f, 32.40f},
    {1687.16f, 4929.39f, 42.08f},
    {1701.31f, 6416.03f, 32.76f},
    {179.86f, 6602.84f, 31.87f},
    {-94.46f, 6419.59f, 31.49f},
    {-2555.00f, 2334.40f, 33.08f},
    {-1800.38f, 803.66f, 138.65f},
    {-1437.62f, -276.75f, 46.21f},
    {-2096.24f, -320.29f, 13.17f},
    {-724.62f, -935.16f, 19.21f},
    {-526.02f, -1211.00f, 18.18f},
    {-70.21f, -1761.79f, 29.53f},
    {819.65f, -1028.85f, 26.40f},
    {1208.95f, -1402.57f, 35.22f},
    {1181.38f, -330.85f, 69.32f},
    {2581.32f, 362.04f, 108.47f},
    {176.63f, -1562.03f, 29.26f},
    {-319.29f, -1471.72f, 30.55f},
}};

constexpr const char *kPumpAnimDict = "timetable@gardener@filling_can";
constexpr const char *kPumpAnimName = "gar_ig_5_filling_can";
constexpr const char *kCanAnimDict = "weapon@w_sp_jerrycan";
constexpr const char *kCanAnimName = "fire";

void RemoveStationBlips() {
  for (Blip &blip : s_stationBlips) {
    if (blip && HUD::DOES_BLIP_EXIST(blip))
      HUD::REMOVE_BLIP(&blip);
  }
  s_stationBlips.clear();
}

void UpdateStationBlips() {
  if (!Config::FuelEnabled || !Config::FuelBlipsEnabled) {
    if (!s_stationBlips.empty())
      RemoveStationBlips();
    return;
  }
  if (!s_stationBlips.empty())
    return;

  s_stationBlips.reserve(kStations.size());
  for (const Station &station : kStations) {
    Blip blip = HUD::ADD_BLIP_FOR_COORD(station.x, station.y, station.z);
    if (!blip)
      continue;
    HUD::SET_BLIP_SPRITE(blip, 361);
    HUD::SET_BLIP_COLOUR(blip, 5);
    HUD::SET_BLIP_SCALE(blip, 0.72f);
    HUD::SET_BLIP_AS_SHORT_RANGE(blip, TRUE);
    HUD::BEGIN_TEXT_COMMAND_SET_BLIP_NAME("STRING");
    HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME("Pom bensin");
    HUD::END_TEXT_COMMAND_SET_BLIP_NAME(blip);
    s_stationBlips.push_back(blip);
  }
  LOG_INFO(Fuel, "Fuel minimap blips ready count=%zu",
           s_stationBlips.size());
}

float Distance(Vector3 a, Vector3 b) {
  const float x = a.x - b.x;
  const float y = a.y - b.y;
  const float z = a.z - b.z;
  return std::sqrt(x * x + y * y + z * z);
}

Object FindPump(Vector3 pos) {
  static const std::array<const char *, 7> names = {
      "prop_gas_pump_1a", "prop_gas_pump_1b", "prop_gas_pump_1c",
      "prop_gas_pump_1d", "prop_vintage_pump", "prop_gas_pump_old2",
      "prop_gas_pump_old3"};
  Object closest = 0;
  float best = 1000.0f;
  for (const char *name : names) {
    const Hash hash = MISC::GET_HASH_KEY(name);
    const Object pump = OBJECT::GET_CLOSEST_OBJECT_OF_TYPE(
        pos.x, pos.y, pos.z, 6.5f, hash, FALSE, FALSE, FALSE);
    if (!pump || !ENTITY::DOES_ENTITY_EXIST(pump))
      continue;
    const float distance =
        Distance(pos, ENTITY::GET_ENTITY_COORDS(pump, TRUE));
    if (distance < best) {
      best = distance;
      closest = pump;
    }
  }
  return closest;
}

void DeleteProp() {
  if (s_prop && ENTITY::DOES_ENTITY_EXIST(s_prop)) {
    ENTITY::DETACH_ENTITY(s_prop, TRUE, TRUE);
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(s_prop, TRUE, TRUE);
    OBJECT::DELETE_OBJECT(&s_prop);
  }
  s_prop = 0;
}

void Stop(Ped player, bool clearTasks) {
  FuelSystem::StopRefuel();
  DeleteProp();
  if (clearTasks && player && ENTITY::DOES_ENTITY_EXIST(player))
    TASK::CLEAR_PED_TASKS(player);
  STREAMING::REMOVE_ANIM_DICT(kPumpAnimDict);
  STREAMING::REMOVE_ANIM_DICT(kCanAnimDict);
  s_phase = Phase::Idle;
  s_phaseTick = 0;
  s_pump = 0;
  s_usingJerryCan = false;
  s_promptVisible = false;
}

bool HasJerryCan(Ped player) {
  return WEAPON::HAS_PED_GOT_WEAPON(
             player, MISC::GET_HASH_KEY("WEAPON_PETROLCAN"), FALSE) != FALSE;
}

void DrawPrompt(bool engineRunning, bool hasSource) {
  if (!hasSource)
    return;
  s_promptVisible = true;
  if (engineRunning) {
    Renderer::DrawInteractionPanel(
        "FUEL", "Matikan mesin dulu sebelum isi bensin", -1.0f);
  } else {
    Renderer::DrawInteractionPanel(
        "FUEL", "Tahan tombol isi bensin", FuelSystem::GetFuelLevel());
  }
}

} // namespace

void Reset() {
  Stop(PLAYER::PLAYER_PED_ID(), false);
  s_vehicle = 0;
}

void TrackVehicle(Vehicle vehicle) {
  if (vehicle && ENTITY::DOES_ENTITY_EXIST(vehicle))
    s_vehicle = vehicle;
}

void Update(Ped player) {
  UpdateStationBlips();
  s_promptVisible = s_phase != Phase::Idle;
  if (!Config::FuelEnabled || !player ||
      !ENTITY::DOES_ENTITY_EXIST(player) || PED::IS_PED_INJURED(player)) {
    if (s_phase != Phase::Idle)
      Stop(player, true);
    return;
  }

  if (PED::IS_PED_IN_ANY_VEHICLE(player, FALSE)) {
    if (s_phase != Phase::Idle)
      Stop(player, true);
    const Vehicle current = PED::GET_VEHICLE_PED_IS_USING(player);
    if (current && VEHICLE::GET_PED_IN_VEHICLE_SEAT(current, -1, FALSE) ==
                       player)
      TrackVehicle(current);
    return;
  }

  if (!s_vehicle || !ENTITY::DOES_ENTITY_EXIST(s_vehicle)) {
    if (s_phase != Phase::Idle)
      Stop(player, true);
    return;
  }

  const Vector3 playerPos = ENTITY::GET_ENTITY_COORDS(player, TRUE);
  const Vector3 vehiclePos = ENTITY::GET_ENTITY_COORDS(s_vehicle, TRUE);
  const float vehicleDistance = Distance(playerPos, vehiclePos);
  if (vehicleDistance > 5.0f) {
    if (s_phase != Phase::Idle)
      Stop(player, true);
    return;
  }

  if (!s_pump || !ENTITY::DOES_ENTITY_EXIST(s_pump))
    s_pump = FindPump(playerPos);
  const bool hasPump = s_pump && ENTITY::DOES_ENTITY_EXIST(s_pump);
  const bool hasCan = HasJerryCan(player);
  const bool engineRunning =
      VEHICLE::GET_IS_VEHICLE_ENGINE_RUNNING(s_vehicle) != FALSE;
  const bool keyDown =
      (GetAsyncKeyState(Config::KeyRefuel) & 0x8000) != 0;
  const bool oilServiceKeyDown =
      (GetAsyncKeyState(Config::KeyOilService) & 0x8000) != 0;

  if (s_phase == Phase::Idle) {
    if (oilServiceKeyDown) {
      s_promptVisible = false;
      return;
    }
    DrawPrompt(engineRunning, hasPump || hasCan);
    if (!keyDown || engineRunning || (!hasPump && !hasCan) ||
        FuelSystem::GetFuelLevel() >= 0.999f)
      return;

    s_usingJerryCan = !hasPump && hasCan;
    const char *dict = s_usingJerryCan ? kCanAnimDict : kPumpAnimDict;
    STREAMING::REQUEST_ANIM_DICT(dict);
    if (!s_usingJerryCan)
      STREAMING::REQUEST_MODEL(MISC::GET_HASH_KEY("prop_cs_fuel_nozle"));
    TASK::TASK_TURN_PED_TO_FACE_ENTITY(player, s_vehicle, 650);
    s_phase = Phase::Loading;
    s_phaseTick = GetTickCount64();
    LOG_INFO(Fuel, "Refuel prepare vehicle=%d source=%s", s_vehicle,
             s_usingJerryCan ? "jerrycan" : "pump");
    return;
  }

  if (!keyDown || engineRunning || vehicleDistance > 5.0f) {
    Stop(player, true);
    return;
  }

  const char *dict = s_usingJerryCan ? kCanAnimDict : kPumpAnimDict;
  const char *anim = s_usingJerryCan ? kCanAnimName : kPumpAnimName;
  if (s_phase == Phase::Loading) {
    STREAMING::REQUEST_ANIM_DICT(dict);
    const Hash nozzleModel = MISC::GET_HASH_KEY("prop_cs_fuel_nozle");
    if (!s_usingJerryCan)
      STREAMING::REQUEST_MODEL(nozzleModel);
    const bool animReady = STREAMING::HAS_ANIM_DICT_LOADED(dict) != FALSE;
    const bool propReady =
        s_usingJerryCan || STREAMING::HAS_MODEL_LOADED(nozzleModel) != FALSE;
    if ((!animReady || !propReady) &&
        GetTickCount64() - s_phaseTick < 2500) {
      Renderer::DrawInteractionPanel("FUEL", "Menyiapkan selang...", 0.0f);
      s_promptVisible = true;
      return;
    }

    if (s_usingJerryCan) {
      WEAPON::SET_CURRENT_PED_WEAPON(
          player, MISC::GET_HASH_KEY("WEAPON_PETROLCAN"), TRUE);
    } else if (propReady) {
      s_prop = OBJECT::CREATE_OBJECT(
          nozzleModel, playerPos.x, playerPos.y, playerPos.z,
          FALSE, FALSE, FALSE);
      if (s_prop) {
        ENTITY::SET_ENTITY_COLLISION(s_prop, FALSE, FALSE);
        const int hand = PED::GET_PED_BONE_INDEX(player, 57005);
        ENTITY::ATTACH_ENTITY_TO_ENTITY(
            s_prop, player, hand, 0.12f, 0.02f, -0.01f,
            -80.0f, -20.0f, 15.0f, TRUE, TRUE, FALSE, TRUE, 2, TRUE, 0);
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(nozzleModel);
      }
    }
    if (animReady)
      TASK::TASK_PLAY_ANIM(player, dict, anim, 2.0f, -2.0f, -1, 49,
                           0.0f, FALSE, FALSE, FALSE);
    FuelSystem::StartRefuel();
    s_phase = Phase::Refueling;
    LOG_INFO(Fuel, "Refuel active vehicle=%d", s_vehicle);
  }

  if (s_phase == Phase::Refueling) {
    const float dt = std::clamp(MISC::GET_FRAME_TIME(), 0.001f, 0.05f);
    FuelSystem::AddFuel(Config::RefuelRatePerSecond * dt);
    Renderer::DrawInteractionPanel(
        "REFUELING", s_usingJerryCan ? "Jerigen" : "Pom bensin",
        FuelSystem::GetFuelLevel());
    s_promptVisible = true;
    if (FuelSystem::GetFuelLevel() >= 0.999f) {
      Stop(player, true);
      Renderer::ShowNotification("~g~Tangki penuh");
      LOG_INFO(Fuel, "Refuel completed vehicle=%d", s_vehicle);
    }
  }
}

bool IsActive() { return s_phase != Phase::Idle; }
bool IsPromptVisible() { return s_promptVisible; }
Vehicle GetTrackedVehicle() { return s_vehicle; }

} // namespace RefuelInteraction
