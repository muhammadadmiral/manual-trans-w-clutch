#include "WorkshopIntegration.h"

#include "MaintenanceSystem.h"
#include "WorkshopTuning.h"
#include "../Clutch/ClutchSystem.h"
#include "../Gearbox/Automatic/AutomaticGearbox.h"
#include "../Gearbox/Core/GearboxSystem.h"
#include "../VehicleUpgrades.h"
#include "../../Core/Config.h"
#include "../../Core/Menu.h"
#include "../../Core/Renderer.h"
#include "../../../sdk/inc/natives.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

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

constexpr int kItemCount = 16;

void Notify(const char *message) {
  Renderer::ShowNotification(message);
}

// Menghitung kuadrat jarak 3D untuk optimasi (menghindari operasi sqrt yang mahal)
float DistanceSquared(const Vector3 &a, const ServiceBay &b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  const float dz = a.z - b.z;
  return dx * dx + dy * dy + dz * dz;
}

// Mencari service bay terdekat dari kendaraan pemain yang berada di dalam batas radius
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

// Menggambar antarmuka UI Service Bay dan informasi kendaraan di layar
void DrawPanel(bool engineOn) {
  const float x = 0.50f;
  const float y = 0.48f;
  const float width = 0.54f;
  const float rowHeight = 0.038f;
  const float height = 0.132f + rowHeight * kItemCount;
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
      "Pedal / Throttle Map",
      "Clutch Package",
      "Flywheel Package",
      "Transmission Calibration",
      "Low-Speed Creep",
      "Cruise / Coast Map",
      "Drivetrain Mounts",
      "TCS Calibration",
      "ABS Calibration",
      "Launch Calibration",
      "Engine Oil + Filter",
      "Gearbox / Synchro Rebuild",
      "Clutch Surface Service",
      "ATF + Adaptation Reset",
      "Complete Driveline Inspection",
      "Close Service Bay",
  };
  const char *details[kItemCount] = {
      "Enam response map per model: factory sampai crawl dan eco.",
      "Kapasitas torsi, bite, heat soak, dan cooling clutch.",
      "Inersia mesin saat naik/turun RPM tanpa mengubah redline.",
      "Street/Sport/Race mengikuti rasio handling native kendaraan.",
      "Kalibrasi gerak pelan untuk macet, parkir, dan crawling.",
      "Karakter pedal ringan dan engine-braking saat cruising.",
      "Compliance drivetrain, clunk, dan suspension load transfer.",
      "Slip target dan kekuatan torque intervention TCS.",
      "Wheel-slip target, release pressure, dan pulse ABS.",
      "Target RPM serta agresivitas soft-cut launch control.",
      "Pulihkan umur oli dan kualitas pelumasan mesin.",
      "Pulihkan health gearbox dan synchronizer.",
      "Hilangkan heat, slip, dan judder permukaan clutch.",
      "Pulihkan temperatur ATF dan adaptation automatic gearbox.",
      "Servis oli, gearbox, clutch, ATF, dan adaptation sekaligus.",
      "Tutup panel modifikasi Melar x Los Santos Customs.",
  };

  for (int i = 0; i < kItemCount; ++i) {
    const float rowY = top + 0.082f + i * rowHeight;
    const bool selected = i == s_selected;
    GRAPHICS::DRAW_RECT(
        x, rowY + rowHeight * 0.5f, width - 0.014f, rowHeight - 0.002f,
        selected ? 22 : (i < 10 ? 12 : 18),
        selected ? 70 : (i < 10 ? 24 : 20),
        selected ? 94 : (i < 10 ? 32 : 25),
        selected ? 245 : 220, 0);
    if (selected) {
      GRAPHICS::DRAW_RECT(left + 0.010f, rowY + rowHeight * 0.5f,
                          0.004f, rowHeight - 0.002f,
                          55, 205, 255, 255, 0);
    }
    Renderer::DrawTextOverlay(
        names[i], left + 0.018f, rowY + 0.006f, 0.285f,
        selected ? 255 : 210, selected ? 255 : 222,
        selected ? 255 : 232, 255, 0, false, false);

    char value[64]{};
    switch (i) {
    case 0:
      sprintf_s(value, "< %s >",
                WorkshopTuning::GetLabel(
                    WorkshopTuning::Option::PedalMap));
      break;
    case 1:
      sprintf_s(value, "< %s >",
                WorkshopTuning::GetLabel(
                    WorkshopTuning::Option::ClutchPackage));
      break;
    case 2:
      sprintf_s(value, "< %s >",
                WorkshopTuning::GetLabel(
                    WorkshopTuning::Option::Flywheel));
      break;
    case 3:
      sprintf_s(value, "< %s >",
                WorkshopTuning::GetLabel(
                    WorkshopTuning::Option::Transmission));
      break;
    case 4:
      sprintf_s(value, "< %s >",
                WorkshopTuning::GetLabel(
                    WorkshopTuning::Option::CreepCalibration));
      break;
    case 5:
      sprintf_s(value, "< %s >",
                WorkshopTuning::GetLabel(
                    WorkshopTuning::Option::CruiseCalibration));
      break;
    case 6:
      sprintf_s(value, "< %s >",
                WorkshopTuning::GetLabel(
                    WorkshopTuning::Option::DrivetrainMounts));
      break;
    case 7:
      sprintf_s(value, "< %s >",
                WorkshopTuning::GetLabel(
                    WorkshopTuning::Option::TcsCalibration));
      break;
    case 8:
      sprintf_s(value, "< %s >",
                WorkshopTuning::GetLabel(
                    WorkshopTuning::Option::AbsCalibration));
      break;
    case 9:
      sprintf_s(value, "< %s >",
                WorkshopTuning::GetLabel(
                    WorkshopTuning::Option::LaunchCalibration));
      break;
    case 10:
      sprintf_s(value, "%d%%",
                static_cast<int>(
                    MaintenanceSystem::GetState().oilLife * 100.0f));
      break;
    case 11:
      sprintf_s(value, "%d%%",
                static_cast<int>(GearboxSystem::GetHealth() * 100.0f));
      break;
    case 12:
      sprintf_s(value, "HEAT %d%%",
                static_cast<int>(ClutchSystem::GetHeat() * 100.0f));
      break;
    case 13:
      sprintf_s(value, "%s",
                AutomaticGearbox::GetState().limpMode ? "LIMP" : "READY");
      break;
    case 14:
      sprintf_s(value, "SERVICE ALL");
      break;
    default:
      break;
    }
    if (value[0]) {
      Renderer::DrawTextOverlay(
          value, left + width - 0.105f, rowY + 0.006f, 0.275f,
          selected ? 115 : 145, selected ? 225 : 165,
          selected ? 255 : 180, 255, 0, false, true);
    }
  }

  Renderer::DrawTextOverlay(
      details[s_selected], x,
      top + 0.086f + rowHeight * kItemCount, 0.245f,
      195, 210, 225, 245, 0, false, true);
  Renderer::DrawTextOverlay(
      "UP/DOWN NAVIGATE   LEFT/RIGHT TUNE   ENTER APPLY   BACK EXIT",
      x, top + 0.110f + rowHeight * kItemCount, 0.235f,
      145, 160, 175, 235, 0, false, true);
}

// Mengubah nilai konfigurasi/opsi menu ke kiri (-1) atau kanan (+1)
void AdjustSelected(int direction) {
  if (s_selected < 0 || s_selected > 9)
    return;
  WorkshopTuning::Adjust(
      static_cast<WorkshopTuning::Option>(s_selected), direction);
  VehicleUpgrades::Initialize(s_vehicle);
}

// Mengeksekusi servis mekanikal atau mengubah pengaturan saat tombol ENTER/SELECT ditekan
void ActivateSelected(bool engineOn) {
  if (s_selected >= 10 && s_selected <= 14 && engineOn) {
    Notify("~r~Service locked:~w~ matikan mesin terlebih dahulu");
    return;
  }
  switch (s_selected) {
  case 0: case 1: case 2: case 3: case 4:
  case 5: case 6: case 7: case 8: case 9:
    AdjustSelected(1);
    Notify("~b~LSC tune applied:~w~ profile disimpan per model kendaraan");
    break;
  case 10:
    MaintenanceSystem::ServiceOil();
    Notify("~g~LSC:~w~ oli dan filter sudah diganti");
    break;
  case 11:
    GearboxSystem::ServiceGearbox();
    Notify("~g~LSC:~w~ gearbox dan synchronizer direbuild");
    break;
  case 12:
    ClutchSystem::ServiceClutch();
    Notify("~g~LSC:~w~ clutch/flywheel selesai diservis");
    break;
  case 13:
    AutomaticGearbox::ServiceTransmission();
    Notify("~g~LSC:~w~ ATF dan transmission adaptation direset");
    break;
  case 14:
    MaintenanceSystem::ServiceOil();
    GearboxSystem::ServiceGearbox();
    ClutchSystem::ServiceClutch();
    AutomaticGearbox::ServiceTransmission();
    Notify("~g~LSC:~w~ complete driveline inspection selesai");
    break;
  case 15:
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

// Fungsi utama yang dipanggil setiap frame untuk menangani deteksi jarak bengkel,
// input pemain (buka menu, navigasi), dan mengunci pergerakan kendaraan.
bool Update(Ped playerPed, Vehicle vehicle, bool engineOn) {
  if (!Config::WorkshopEnabled || !vehicle ||
      !ENTITY::DOES_ENTITY_EXIST(vehicle) ||
      VEHICLE::GET_PED_IN_VEHICLE_SEAT(vehicle, -1, 0) != playerPed) {
    Reset();
    return false;
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
    return false;
  }

  if (vehicle != s_vehicle || !s_near || Menu::IsOpen()) {
    s_open = false;
    return false;
  }

  // Nonaktifkan kontrol kendaraan dan menu navigasi standar GTA
  // (59-64: Steering/Movement, 71-72: Gas/Brake, 75-76: Exit/Handbrake, 172-177: UI Navigation)
  const int controls[] = {
      59, 60, 63, 64, 71, 72, 75, 76, 172, 173, 174, 175, 176, 177};
  for (const int control : controls)
    PAD::DISABLE_CONTROL_ACTION(0, control, TRUE);
  
  // Memaksa pedal rem (kontrol 72) ditekan penuh agar mobil tidak menggelinding saat di menu
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
  return s_open;
}

bool IsOpen() { return s_open; }
bool IsNearServiceBay() { return s_near; }

} // namespace WorkshopIntegration
