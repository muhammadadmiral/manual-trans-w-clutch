#include "WorkshopIntegration.h"

#include "MaintenanceSystem.h"
#include "../Clutch/ClutchSystem.h"
#include "../Gears/AutomaticGearbox.h"
#include "../Gears/GearboxSystem.h"
#include "../../Core/Config.h"
#include "../../Core/Menu.h"
#include "../../Core/Renderer.h"
#include "../../../sdk/inc/natives.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

extern HMODULE g_pluginModule;

namespace WorkshopIntegration {
namespace {

struct ServiceBay {
  float x;
  float y;
  float z;
  const char *name;
};

// Los Santos Customs, Beeker's Garage, dan Benny's. Radius dapat diubah dari
// INI/menu agar map overhaul masih bisa memakai service bay terdekat.
constexpr std::array<ServiceBay, 6> kServiceBays{{
    {-337.0f, -136.9f, 39.0f, "LS CUSTOMS - BURTON"},
    {731.8f, -1088.8f, 22.2f, "LS CUSTOMS - LA MESA"},
    {-1145.9f, -1991.1f, 13.2f, "LS CUSTOMS - LSIA"},
    {1175.0f, 2640.3f, 37.8f, "LS CUSTOMS - HARMONY"},
    {110.4f, 6626.1f, 31.8f, "BEEKER'S GARAGE"},
    {-205.7f, -1308.8f, 31.3f, "BENNY'S MOTORWORKS"},
}};

bool s_open = false;
bool s_near = false;
bool s_keyWasDown = false;
int s_selected = 0;
Vehicle s_vehicle = 0;
const ServiceBay *s_activeBay = nullptr;

constexpr int kItemCount = 9;

void Notify(const char *message) {
  Renderer::ShowNotification(message);
}

float DistanceSquared(const Vector3 &a, const ServiceBay &b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  const float dz = a.z - b.z;
  return dx * dx + dy * dy + dz * dz;
}

const ServiceBay *FindNearest(Vehicle vehicle) {
  const Vector3 position = ENTITY::GET_ENTITY_COORDS(vehicle, TRUE);
  const float radius =
      std::clamp(Config::WorkshopRadius, 6.0f, 30.0f);
  const float radiusSquared = radius * radius;
  const ServiceBay *nearest = nullptr;
  float best = radiusSquared;
  for (const auto &bay : kServiceBays) {
    const float distance = DistanceSquared(position, bay);
    if (distance <= best) {
      best = distance;
      nearest = &bay;
    }
  }
  return nearest;
}

void DrawPanel(bool engineOn) {
  const float x = 0.50f;
  const float y = 0.48f;
  const float width = 0.36f;
  const float rowHeight = 0.038f;
  const float height = 0.105f + rowHeight * kItemCount;
  const float top = y - height * 0.5f;
  const float left = x - width * 0.5f;
  GRAPHICS::DRAW_RECT(x + 0.004f, y + 0.005f, width + 0.008f,
                      height + 0.010f, 0, 0, 0, 135, 0);
  GRAPHICS::DRAW_RECT(x, y, width, height, 8, 12, 18, 245, 0);
  GRAPHICS::DRAW_RECT(x, top + 0.002f, width, 0.004f,
                      55, 205, 255, 255, 0);
  Renderer::DrawTextOverlay(
      s_activeBay ? s_activeBay->name : "MELAR SERVICE BAY",
      x, top + 0.016f, 0.42f, 238, 246, 255, 255, 0, true, true);
  Renderer::DrawTextOverlay(
      engineOn ? "~r~ENGINE ON~w~ - service mekanikal dikunci"
               : "~g~ENGINE OFF~w~ - vehicle ready for service",
      x, top + 0.050f, 0.27f, 190, 205, 220, 255, 0, false, true);

  const char *names[kItemCount] = {
      "Engine Oil Service",
      "Gearbox Rebuild",
      "Clutch / Flywheel Service",
      "ATF / Transmission Service",
      "Drivetrain Setup",
      "Pedal Map",
      "Traction Control",
      "ABS Calibration",
      "Close Service Bay",
  };

  for (int i = 0; i < kItemCount; ++i) {
    const float rowY = top + 0.082f + i * rowHeight;
    const bool selected = i == s_selected;
    GRAPHICS::DRAW_RECT(
        x, rowY + rowHeight * 0.5f, width - 0.014f, rowHeight - 0.002f,
        selected ? 22 : 12, selected ? 70 : 18, selected ? 94 : 25,
        selected ? 245 : 220, 0);
    if (selected) {
      GRAPHICS::DRAW_RECT(left + 0.010f, rowY + rowHeight * 0.5f,
                          0.004f, rowHeight - 0.002f,
                          55, 205, 255, 255, 0);
    }
    Renderer::DrawTextOverlay(
        names[i], left + 0.018f, rowY + 0.006f, 0.31f,
        selected ? 255 : 210, selected ? 255 : 222,
        selected ? 255 : 232, 255, 0, false, false);

    char value[64]{};
    switch (i) {
    case 0:
      sprintf_s(value, "%d%%",
                static_cast<int>(MaintenanceSystem::GetState().oilLife *
                                 100.0f));
      break;
    case 1:
      sprintf_s(value, "%d%%",
                static_cast<int>(GearboxSystem::GetHealth() * 100.0f));
      break;
    case 2:
      sprintf_s(value, "HEAT %d%%",
                static_cast<int>(ClutchSystem::GetHeat() * 100.0f));
      break;
    case 3:
      sprintf_s(value, "%s",
                AutomaticGearbox::GetState().limpMode ? "LIMP" : "CHECK");
      break;
    case 4: {
      const char *modes[] = {"OFF", "AUTOMATIC", "MANUAL"};
      sprintf_s(value, "< %s >",
                modes[std::clamp(Config::TransmissionMode, 0, 2)]);
      break;
    }
    case 5: {
      const char *presets[] =
          {"DEFAULT", "RESPONSIVE", "SMOOTH", "SIM RACING", "CUSTOM"};
      sprintf_s(value, "< %s >",
                presets[std::clamp(Config::PedalPreset, 0, 4)]);
      break;
    }
    case 6:
      sprintf_s(value, "%s", Config::TcsEnabled ? "ON" : "OFF");
      break;
    case 7:
      sprintf_s(value, "%s", Config::AbsEnabled ? "ON" : "OFF");
      break;
    default:
      break;
    }
    if (value[0]) {
      Renderer::DrawTextOverlay(
          value, left + width - 0.078f, rowY + 0.006f, 0.29f,
          selected ? 115 : 145, selected ? 225 : 165,
          selected ? 255 : 180, 255, 0, false, true);
    }
  }

  Renderer::DrawTextOverlay(
      "UP/DOWN navigate   LEFT/RIGHT tune   ENTER service   BACK exit",
      x, top + 0.088f + rowHeight * kItemCount, 0.255f,
      145, 160, 175, 235, 0, false, true);
}

void AdjustSelected(int direction) {
  if (s_selected == 4) {
    Config::TransmissionMode =
        std::clamp(Config::TransmissionMode + direction, 0, 2);
    Config::SaveConfig(g_pluginModule);
  } else if (s_selected == 5) {
    const int next =
        std::clamp(Config::PedalPreset + direction, 0, 3);
    Config::ApplyPedalPreset(next);
    Config::SaveConfig(g_pluginModule);
  }
}

void ActivateSelected(bool engineOn) {
  if (s_selected <= 3 && engineOn) {
    Notify("~r~Service locked:~w~ matikan mesin terlebih dahulu");
    return;
  }
  switch (s_selected) {
  case 0:
    MaintenanceSystem::ServiceOil();
    Notify("~g~LSC:~w~ oli dan filter sudah diganti");
    break;
  case 1:
    GearboxSystem::ServiceGearbox();
    Notify("~g~LSC:~w~ gearbox dan synchronizer direbuild");
    break;
  case 2:
    ClutchSystem::ServiceClutch();
    Notify("~g~LSC:~w~ clutch/flywheel selesai diservis");
    break;
  case 3:
    AutomaticGearbox::ServiceTransmission();
    Notify("~g~LSC:~w~ ATF dan transmission adaptation direset");
    break;
  case 6:
    Config::TcsEnabled = !Config::TcsEnabled;
    Config::SaveConfig(g_pluginModule);
    break;
  case 7:
    Config::AbsEnabled = !Config::AbsEnabled;
    Config::SaveConfig(g_pluginModule);
    break;
  case 8:
    s_open = false;
    break;
  default:
    break;
  }
}

} // namespace

void Reset() {
  s_open = false;
  s_near = false;
  s_keyWasDown = false;
  s_selected = 0;
  s_vehicle = 0;
  s_activeBay = nullptr;
}

void Update(Ped playerPed, Vehicle vehicle, bool engineOn) {
  if (!Config::WorkshopEnabled || !vehicle ||
      !ENTITY::DOES_ENTITY_EXIST(vehicle) ||
      VEHICLE::GET_PED_IN_VEHICLE_SEAT(vehicle, -1, 0) != playerPed) {
    Reset();
    return;
  }

  s_activeBay = FindNearest(vehicle);
  s_near = s_activeBay != nullptr;
  const bool keyDown =
      (GetAsyncKeyState(Config::KeyWorkshop) & 0x8000) != 0;
  const bool keyPressed = keyDown && !s_keyWasDown;
  s_keyWasDown = keyDown;

  if (!s_open) {
    if (s_near && ENTITY::GET_ENTITY_SPEED(vehicle) < 2.0f &&
        !Menu::IsOpen()) {
      Renderer::DrawInteractionPanel(
          "MELAR x LOS SANTOS CUSTOMS",
          "Berhenti lalu tekan tombol Workshop untuk service & tuning",
          -1.0f);
      if (keyPressed) {
        s_open = true;
        s_vehicle = vehicle;
        s_selected = 0;
      }
    }
    return;
  }

  if (vehicle != s_vehicle || !s_near || Menu::IsOpen()) {
    s_open = false;
    return;
  }

  const int controls[] = {
      59, 60, 63, 64, 71, 72, 75, 76, 172, 173, 174, 175, 176, 177};
  for (const int control : controls)
    PAD::DISABLE_CONTROL_ACTION(0, control, TRUE);
  PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, 72, 1.0f);

  if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 172))
    s_selected = (s_selected + kItemCount - 1) % kItemCount;
  if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 173))
    s_selected = (s_selected + 1) % kItemCount;
  if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 174))
    AdjustSelected(-1);
  if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 175))
    AdjustSelected(1);
  if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 176))
    ActivateSelected(engineOn);
  if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 177))
    s_open = false;

  DrawPanel(engineOn);
}

bool IsOpen() { return s_open; }
bool IsNearServiceBay() { return s_near; }

} // namespace WorkshopIntegration
