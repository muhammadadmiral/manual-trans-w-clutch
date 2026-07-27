#pragma once

#include <cstdint>

namespace Renderer {

struct SpeedometerData {
  float speedKmH = 0.0f;
  float normalizedRPM = 0.0f;
  float physicalRPM = 0.0f;
  float redlineRPM = 7000.0f;
  float fuel = 0.0f;
  float oilTemperature = 0.0f;
  float oilLife = 0.0f;
  float engineHealth = 0.0f;
  float gearboxHealth = 0.0f;
  float clutchHeat = 0.0f;
  float boost = 0.0f;
  float odometerKm = 0.0f;
  float throttle = 0.0f;
  float brake = 0.0f;
  int gear = 0;
  int maxGear = 0;
  int transmissionMode = 0;
  const char *automaticSelector = nullptr;
  bool motorcycle = false;
  bool electric = false;
  bool engineOn = false;
  bool engineStarting = false;
  bool parkingBrake = false;
  bool tcsActive = false;
  bool absActive = false;
  bool escActive = false;
  bool rollWarning = false;
  bool launchControl = false;
  bool burnout = false;
  int vehicleClass = 0;
  std::uint32_t modelHash = 0;
};

void ShowNotification(const char *message);

void DrawTextOverlay(const char *text, float x, float y, float scale = 0.42f,
                     int r = 255, int g = 255, int b = 255, int a = 255,
                     int font = 0, bool outline = true, bool center = false);

void DrawBar(float x, float y, float width, float height, float fraction, int r,
             int g, int b, const char *label);

void DrawGearHUD(int manualGear, int maxGear, int activeSignal, bool isEngineOn,
                 bool engineStarting, int transmissionMode = 2,
                 const char *automaticSelector = nullptr);

void DrawGrindWarning();

void DrawDebugOverlay(int manualGear, unsigned gameGear, unsigned nextGear,
                      float rpm, float clutch, const char *srcName);

void DrawPedalsOverlay(float rpm, float clutch, float throttle, float brake);

void DrawSimulationOverlay(float fuel, float oilTemp, float oilLife,
                            float gearboxHealth,
                            float clutchHeat, bool parkingBrake,
                            bool wheelsLocked, float engineBrake);

void DrawSpeedometer(const SpeedometerData &data);

void DrawInteractionPanel(const char *title, const char *detail,
                          float progress);

} // namespace Renderer
