#include "SpeedometerRenderer.h"
#include "SpeedometerIcons.h"

#include "../../Core/Config.h"
#include "../../Core/Renderer.h"
#include "../../../sdk/inc/natives.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace Speedometer {
namespace {

using Colour = SpeedometerIcons::Colour;

struct SafeRect {
  float left;
  float top;
  float right;
  float bottom;
  float aspect;
};

SafeRect GetSafeRect() {
  const float safe =
      std::clamp(GRAPHICS::GET_SAFE_ZONE_SIZE(), 0.80f, 1.0f);
  const float inset = (1.0f - safe) * 0.50f + 0.012f;
  const float aspect =
      std::clamp(GRAPHICS::GET_ASPECT_RATIO(FALSE), 1.25f, 3.60f);
  return {inset, inset, 1.0f - inset, 1.0f - inset, aspect};
}

int ResolveStyle(const Data &data, int configuredStyle) {
  if (configuredStyle != 9)
    return std::clamp(configuredStyle, 0, 8);
  if (data.motorcycle) {
    if (data.electric)
      return 5;
    if (data.vehicleClass == 9)
      return 6;
    return static_cast<int>(
        (data.modelHash + static_cast<unsigned>(data.maxGear)) % 9u);
  }
  if (data.electric)
    return 0;
  switch (data.vehicleClass) {
  case 4:
  case 5:
    return data.modelHash % 2u == 0u ? 1 : 3;
  case 6:
  case 7:
    return 2;
  case 10:
  case 11:
  case 12:
    return 3;
  default:
    return static_cast<int>(data.modelHash % 9u);
  }
}

Colour ResolveAccent() {
  switch (Config::SpeedometerAccent) {
  case 1: return {255, 70, 65};
  case 2: return {70, 235, 120};
  case 3: return {255, 175, 45};
  case 4: return {235, 240, 245};
  default: return {55, 205, 255};
  }
}

std::string FormatGear(const Data &data) {
  if (!data.engineOn && !data.engineStarting)
    return "OFF";
  if (data.engineStarting)
    return "START";
  if (data.transmissionMode == 1 && data.automaticSelector) {
    std::string text = data.automaticSelector;
    const char selector = data.automaticSelector[0];
    if ((selector == 'D' || selector == 'S') && data.gear > 0)
      text += std::to_string(data.gear);
    return text;
  }
  if (data.gear < 0)
    return "R";
  if (data.gear == 0)
    return "N";
  return std::to_string(data.gear);
}

void DrawArc(float cx, float cy, float radiusX, float radiusY,
             float fraction, const Colour &accent, int opacity, float scale) {
  constexpr float start = 2.443461f;
  constexpr float sweep = 4.537856f;
  fraction = std::clamp(fraction, 0.0f, 1.0f);
  for (int i = 0; i < 31; ++i) {
    const float t = static_cast<float>(i) / 30.0f;
    const float angle = start + sweep * t;
    const bool lit = t <= fraction + 0.001f;
    const bool redline = t > 0.84f;
    GRAPHICS::DRAW_RECT(
        cx + std::cos(angle) * radiusX,
        cy + std::sin(angle) * radiusY,
        (i % 5 == 0 ? 0.0042f : 0.0027f) * scale,
        (i % 5 == 0 ? 0.0060f : 0.0038f) * scale,
        lit ? (redline ? 255 : accent.r) : 48,
        lit ? (redline ? 55 : accent.g) : 52,
        lit ? (redline ? 45 : accent.b) : 58,
        lit ? opacity : opacity / 2, 0);
  }

  const float angle = start + sweep * fraction;
  for (int i = 1; i <= 12; ++i) {
    const float t = static_cast<float>(i) / 12.0f;
    GRAPHICS::DRAW_RECT(
        cx + std::cos(angle) * radiusX * t * 0.78f,
        cy + std::sin(angle) * radiusY * t * 0.78f,
        0.0024f * scale, 0.0036f * scale,
        accent.r, accent.g, accent.b, opacity, 0);
  }
  GRAPHICS::DRAW_RECT(cx, cy, 0.008f * scale, 0.011f * scale,
                      225, 232, 238, opacity, 0);
}

void DrawDial(float cx, float cy, float radiusX, float radiusY,
              float fraction, const char *label, const char *maxMark,
              const Colour &accent, int opacity, float scale) {
  GRAPHICS::DRAW_RECT(cx, cy, radiusX * 2.32f, radiusY * 2.18f,
                      5, 7, 9, opacity, 0);
  DrawArc(cx, cy, radiusX, radiusY, fraction, accent, opacity, scale);
  const float maximum = std::strtof(maxMark, nullptr);
  constexpr float start = 2.443461f;
  constexpr float sweep = 4.537856f;
  for (int mark = 0; mark <= 4; ++mark) {
    const float t = static_cast<float>(mark) / 4.0f;
    const float angle = start + sweep * t;
    char markText[12]{};
    sprintf_s(markText, "%.0f", maximum * t);
    Renderer::DrawTextOverlay(
        markText,
        cx + std::cos(angle) * radiusX * 0.73f,
        cy + std::sin(angle) * radiusY * 0.73f - 0.006f * scale,
        0.135f * scale, 132, 142, 151, opacity,
        0, false, true);
  }
  Renderer::DrawTextOverlay(label, cx, cy + radiusY * 0.34f,
                            0.225f * scale, 165, 174, 182, opacity,
                            0, false, true);
}

void DrawTachBar(float left, float y, float width, float rpm, int segments,
                 const Colour &accent, int opacity, float scale) {
  const float gap = 0.0020f * scale;
  const float segmentWidth =
      (width - gap * static_cast<float>(segments - 1)) /
      static_cast<float>(segments);
  for (int i = 0; i < segments; ++i) {
    const float threshold =
        static_cast<float>(i + 1) / static_cast<float>(segments);
    const bool lit = rpm >= threshold - 0.001f;
    const bool redline = i >= segments - 3;
    GRAPHICS::DRAW_RECT(
        left + i * (segmentWidth + gap) + segmentWidth * 0.5f, y,
        segmentWidth, 0.009f * scale,
        lit ? (redline ? 255 : accent.r) : 32,
        lit ? (redline ? 55 : accent.g) : 37,
        lit ? (redline ? 45 : accent.b) : 43,
        lit ? opacity : opacity / 2, 0);
  }
}

void DrawShiftLights(float x, float y, float width, float rpm,
                     int opacity, float scale) {
  constexpr int count = 10;
  const float gap = width / static_cast<float>(count);
  for (int index = 0; index < count; ++index) {
    const float threshold = 0.52f + static_cast<float>(index) * 0.048f;
    const bool lit = rpm >= threshold;
    const bool red = index >= 8;
    const bool amber = index >= 5 && index < 8;
    const int r = lit ? (red ? 255 : (amber ? 255 : 70)) : 35;
    const int g = lit ? (red ? 52 : (amber ? 176 : 225)) : 40;
    const int b = lit ? (red ? 42 : (amber ? 38 : 112)) : 46;
    GRAPHICS::DRAW_RECT(
        x - width * 0.5f + gap * (static_cast<float>(index) + 0.5f),
        y, 0.0060f * scale, 0.0060f * scale,
        r, g, b, lit ? opacity : opacity / 2, 0);
  }
}

void DrawStatusIcons(const Data &data, float x, float y, float width,
                     const Colour &accent, int opacity, float scale) {
  const float slot = width / 8.0f;
  float cursor = x - width * 0.5f + slot * 0.5f;
  SpeedometerIcons::DrawPower(
      cursor, y, data.engineOn || data.engineStarting,
      accent, opacity, scale);
  cursor += slot;
  SpeedometerIcons::DrawStateBadge(
      "P", cursor, y, true, data.parkingBrake,
      {255, 68, 58}, opacity, scale);
  cursor += slot;
  SpeedometerIcons::DrawStateBadge(
      "TCS", cursor, y, data.tcsEnabled, data.tcsActive,
      {255, 190, 45}, opacity, scale);
  cursor += slot;
  SpeedometerIcons::DrawStateBadge(
      "ABS", cursor, y, data.absEnabled, data.absActive,
      {255, 190, 45}, opacity, scale);
  cursor += slot;
  SpeedometerIcons::DrawStateBadge(
      "ESC", cursor, y, data.escEnabled, data.escActive,
      {255, 190, 45}, opacity, scale);
  cursor += slot;
  SpeedometerIcons::DrawStateBadge(
      "LC", cursor, y, data.launchEnabled, data.launchControl,
      {55, 205, 255}, opacity, scale);
  cursor += slot;
  SpeedometerIcons::DrawStateBadge(
      "BURN", cursor, y, true, data.burnout,
      {255, 112, 40}, opacity, scale);
  const bool masterWarning =
      data.rollWarning || data.fuel < 0.12f ||
      data.oilTemperature > 0.92f || data.oilLife < 0.12f ||
      data.engineHealth < 0.25f || data.gearboxHealth < 0.20f;
  SpeedometerIcons::DrawWarningTriangle(
      x + width * 0.5f - slot * 0.5f, y,
      masterWarning, opacity, scale);
}

} // namespace

void Draw(const Data &data) {
  const SafeRect safe = GetSafeRect();
  const float scale = std::clamp(Config::SpeedometerScale, 0.65f, 1.40f);
  const int opacity = static_cast<int>(
      std::clamp(Config::SpeedometerOpacity, 0.35f, 1.0f) * 255.0f);
  const Colour accent = ResolveAccent();
  const int configuredStyle =
      data.motorcycle ? Config::SpeedometerBikeStyle
                      : Config::SpeedometerCarStyle;
  const int style = ResolveStyle(data, configuredStyle);

  float baseWidth = 0.300f;
  float mainHeight = 0.145f;
  switch (style) {
  case 1:
    baseWidth = 0.330f;
    mainHeight = 0.175f;
    break;
  case 2:
    baseWidth = 0.320f;
    mainHeight = 0.160f;
    break;
  case 3:
    baseWidth = 0.310f;
    mainHeight = 0.160f;
    break;
  case 4:
    baseWidth = 0.250f;
    mainHeight = 0.110f;
    break;
  case 6:
    baseWidth = 0.285f;
    mainHeight = 0.175f;
    break;
  case 7:
    baseWidth = 0.350f;
    mainHeight = 0.180f;
    break;
  case 8:
    baseWidth = 0.330f;
    mainHeight = 0.135f;
    break;
  default:
    break;
  }
  if (data.motorcycle)
    baseWidth -= 0.018f;
  const float statusHeight =
      Config::SpeedometerShowIcons ? 0.030f : 0.006f;
  const float telemetryHeight =
      statusHeight + (Config::SpeedometerDetailed ? 0.059f : 0.0f);
  const float width = baseWidth * scale;
  const float height = (mainHeight + telemetryHeight) * scale;
  const float x = std::clamp(
      Config::SpeedometerPosX, safe.left + width * 0.5f,
      safe.right - width * 0.5f);
  const float y = std::clamp(
      Config::SpeedometerPosY, safe.top + height * 0.5f,
      safe.bottom - height * 0.5f);
  const float left = x - width * 0.5f;
  const float top = y - height * 0.5f;

  GRAPHICS::DRAW_RECT(x + 0.003f * scale, y + 0.004f * scale,
                      width + 0.006f * scale, height + 0.007f * scale,
                      0, 0, 0, opacity / 2, 0);
  GRAPHICS::DRAW_RECT(
      x, y, width, height,
      style == 1 || style == 3 ? 18 : 7,
      style == 1 || style == 3 ? 17 : 11,
      style == 1 || style == 3 ? 15 : 17,
      std::min(245, opacity), 0);
  GRAPHICS::DRAW_RECT(x, top + 0.002f * scale, width, 0.004f * scale,
                      accent.r, accent.g, accent.b, opacity, 0);

  const float rpm = std::clamp(data.normalizedRPM, 0.0f, 1.08f);
  const float shownSpeed =
      Config::SpeedometerUnits == 1 ? data.speedKmH * 0.621371f
                                    : data.speedKmH;
  const char *unit = Config::SpeedometerUnits == 1 ? "MPH" : "KM/H";
  const float requestedMaximumKmH =
      std::clamp(data.maximumSpeedKmH * 1.15f, 140.0f, 440.0f);
  const float requestedMaximum =
      Config::SpeedometerUnits == 1
          ? requestedMaximumKmH * 0.621371f
          : requestedMaximumKmH;
  const float gaugeStep = Config::SpeedometerUnits == 1 ? 20.0f : 40.0f;
  const float maximumSpeed =
      std::ceil(requestedMaximum / gaugeStep) * gaugeStep;
  const float speedFraction =
      std::clamp(shownSpeed / maximumSpeed, 0.0f, 1.0f);
  char speedText[24]{};
  sprintf_s(speedText, "%03d",
            static_cast<int>(std::clamp(shownSpeed, 0.0f, 999.0f) + 0.5f));
  char maximumSpeedText[12]{};
  sprintf_s(maximumSpeedText, "%d", static_cast<int>(maximumSpeed));
  char maximumRpmText[12]{};
  sprintf_s(maximumRpmText, "%.0f",
            std::max(6.0f, data.redlineRPM / 1000.0f));
  char rpmText[32]{};
  sprintf_s(rpmText, "%s %.1f", data.electric ? "POWER" : "RPM x1000",
            data.electric ? rpm * 100.0f
                          : std::max(0.0f, data.physicalRPM) / 1000.0f);
  const std::string gearText = FormatGear(data);

  if (style == 0 || style == 4 || style == 8) {
    DrawTachBar(left + 0.013f * scale, top + 0.017f * scale,
                width - 0.026f * scale, rpm,
                data.motorcycle ? 18 : 16, accent, opacity, scale);
  }

  if (style == 0) {
    Renderer::DrawTextOverlay(
        data.motorcycle ? "ROAD TFT" : "GT DIGITAL",
        left + 0.013f * scale, top + 0.027f * scale, 0.25f * scale,
        130, 145, 160, opacity, 0, false, false);
    Renderer::DrawTextOverlay(speedText, left + 0.012f * scale,
                              top + 0.043f * scale, 0.86f * scale,
                              245, 248, 252, opacity, 2, true, false);
    Renderer::DrawTextOverlay(unit, left + 0.113f * scale,
                              top + 0.084f * scale, 0.27f * scale,
                              145, 160, 175, opacity, 0, false, false);
    GRAPHICS::DRAW_RECT(left + width - 0.052f * scale,
                        top + 0.071f * scale,
                        0.074f * scale, 0.070f * scale,
                        12, 19, 26, opacity, 0);
    Renderer::DrawTextOverlay(gearText.c_str(),
                              left + width - 0.052f * scale,
                              top + 0.045f * scale, 0.80f * scale,
                              accent.r, accent.g, accent.b, opacity,
                              2, true, true);
  } else if (style == 1 && data.motorcycle) {
    const float radiusY = 0.064f * scale;
    DrawDial(x, top + 0.079f * scale, radiusY / safe.aspect,
             radiusY, std::clamp(rpm, 0.0f, 1.0f),
             data.electric ? "POWER" : "RPM x1000",
             data.electric ? "100" : maximumRpmText,
             accent, opacity, scale);
    Renderer::DrawTextOverlay(
        speedText, x, top + 0.054f * scale, 0.58f * scale,
        245, 248, 252, opacity, 2, true, true);
    Renderer::DrawTextOverlay(
        unit, x, top + 0.100f * scale, 0.20f * scale,
        155, 170, 185, opacity, 0, false, true);
    Renderer::DrawTextOverlay(
        gearText.c_str(), x, top + 0.128f * scale, 0.40f * scale,
        accent.r, accent.g, accent.b, opacity, 2, true, true);
  } else if (style == 1) {
    const float dialY = top + 0.079f * scale;
    const float radiusY = 0.052f * scale;
    const float radiusX = radiusY / safe.aspect;
    DrawDial(left + width * 0.29f, dialY, radiusX, radiusY,
             speedFraction, unit, maximumSpeedText,
             accent, opacity, scale);
    DrawDial(left + width * 0.71f, dialY, radiusX, radiusY,
             std::clamp(rpm, 0.0f, 1.0f),
             data.electric ? "POWER" : "RPM x1000",
             data.electric ? "100" : maximumRpmText,
             accent, opacity, scale);
    GRAPHICS::DRAW_RECT(x, top + 0.139f * scale,
                        0.052f * scale, 0.025f * scale,
                        28, 25, 21, opacity, 0);
    Renderer::DrawTextOverlay(gearText.c_str(), x,
                              top + 0.126f * scale, 0.42f * scale,
                              accent.r, accent.g, accent.b, opacity,
                              2, true, true);
    Renderer::DrawTextOverlay(speedText, left + width * 0.29f,
                              dialY - 0.020f * scale, 0.34f * scale,
                              235, 240, 245, opacity, 2, true, true);
    char analogRpm[16]{};
    sprintf_s(analogRpm, "%.1f",
              std::max(0.0f, data.physicalRPM) / 1000.0f);
    Renderer::DrawTextOverlay(analogRpm, left + width * 0.71f,
                              dialY - 0.020f * scale, 0.32f * scale,
                              235, 240, 245, opacity, 2, true, true);
  } else if (style == 2) {
    Renderer::DrawTextOverlay(
        data.motorcycle ? "SPORT BIKE" : "SPORT HYBRID",
        left + 0.014f * scale, top + 0.021f * scale, 0.24f * scale,
        accent.r, accent.g, accent.b, opacity, 0, false, false);
    Renderer::DrawTextOverlay(speedText, left + width * 0.25f,
                              top + 0.048f * scale, 0.76f * scale,
                              245, 248, 252, opacity, 2, true, true);
    Renderer::DrawTextOverlay(unit, left + width * 0.25f,
                              top + 0.093f * scale, 0.23f * scale,
                              145, 160, 175, opacity, 0, false, true);
    Renderer::DrawTextOverlay(gearText.c_str(), left + width * 0.50f,
                              top + 0.058f * scale, 0.85f * scale,
                              accent.r, accent.g, accent.b, opacity,
                              2, true, true);
    const float radiusY = 0.046f * scale;
    DrawDial(left + width * 0.78f, top + 0.078f * scale,
             radiusY / safe.aspect, radiusY,
             std::clamp(rpm, 0.0f, 1.0f),
             data.electric ? "POWER" : "RPM",
             data.electric ? "100" : maximumRpmText,
             accent, opacity, scale);
  } else if (style == 3) {
    Renderer::DrawTextOverlay(
                              data.motorcycle ? "CAFE RACER"
                                              : "RETRO TOURING", x,
                              top + 0.017f * scale, 0.23f * scale,
                              184, 164, 128, opacity, 0, false, true);
    const float radiusY = 0.050f * scale;
    DrawDial(left + width * 0.32f, top + 0.083f * scale,
             radiusY / safe.aspect, radiusY, speedFraction,
             unit, maximumSpeedText, accent, opacity, scale);
    GRAPHICS::DRAW_RECT(left + width * 0.73f, top + 0.079f * scale,
                        0.093f * scale, 0.082f * scale,
                        31, 29, 23, opacity, 0);
    Renderer::DrawTextOverlay(gearText.c_str(), left + width * 0.73f,
                              top + 0.044f * scale, 0.70f * scale,
                              accent.r, accent.g, accent.b, opacity,
                              2, true, true);
    Renderer::DrawTextOverlay(rpmText, left + width * 0.73f,
                              top + 0.098f * scale, 0.22f * scale,
                              190, 174, 145, opacity, 0, false, true);
  } else if (style == 4) {
    Renderer::DrawTextOverlay(speedText, left + 0.012f * scale,
                              top + 0.038f * scale, 0.70f * scale,
                              245, 248, 252, opacity, 2, true, false);
    Renderer::DrawTextOverlay(unit, left + 0.090f * scale,
                              top + 0.068f * scale, 0.22f * scale,
                              145, 160, 175, opacity, 0, false, false);
    Renderer::DrawTextOverlay(gearText.c_str(),
                              left + width - 0.040f * scale,
                              top + 0.035f * scale, 0.72f * scale,
                              accent.r, accent.g, accent.b, opacity,
                              2, true, true);
    Renderer::DrawTextOverlay(rpmText,
                              left + width - 0.076f * scale,
                              top + 0.078f * scale, 0.21f * scale,
                              165, 180, 195, opacity, 0, false, true);
  } else if (style == 5) {
    Renderer::DrawTextOverlay(
        data.motorcycle ? "ARC TFT" : "ARC DIGITAL",
        x, top + 0.015f * scale, 0.23f * scale,
        accent.r, accent.g, accent.b, opacity, 0, false, true);
    const float radiusY = 0.058f * scale;
    DrawArc(x, top + 0.078f * scale, radiusY / safe.aspect,
            radiusY, std::clamp(rpm, 0.0f, 1.0f),
            accent, opacity, scale);
    Renderer::DrawTextOverlay(
        speedText, x, top + 0.050f * scale, 0.66f * scale,
        245, 248, 252, opacity, 2, true, true);
    Renderer::DrawTextOverlay(
        unit, x, top + 0.096f * scale, 0.20f * scale,
        145, 160, 175, opacity, 0, false, true);
    Renderer::DrawTextOverlay(
        gearText.c_str(), left + width - 0.040f * scale,
        top + 0.054f * scale, 0.60f * scale,
        accent.r, accent.g, accent.b, opacity, 2, true, true);
  } else if (style == 6) {
    Renderer::DrawTextOverlay(
        data.motorcycle ? "MOTOCROSS STACK" : "RALLY STACK",
        left + 0.014f * scale, top + 0.015f * scale,
        0.23f * scale, accent.r, accent.g, accent.b,
        opacity, 0, false, false);
    Renderer::DrawTextOverlay(
        gearText.c_str(), left + width * 0.25f,
        top + 0.047f * scale, 0.92f * scale,
        accent.r, accent.g, accent.b, opacity, 2, true, true);
    Renderer::DrawTextOverlay(
        speedText, left + width * 0.64f,
        top + 0.052f * scale, 0.72f * scale,
        245, 248, 252, opacity, 2, true, true);
    Renderer::DrawTextOverlay(
        unit, left + width * 0.64f,
        top + 0.098f * scale, 0.21f * scale,
        145, 160, 175, opacity, 0, false, true);
    DrawTachBar(left + 0.014f * scale, top + 0.135f * scale,
                width - 0.028f * scale, rpm, 20,
                accent, opacity, scale);
  } else if (style == 7) {
    const float radiusY = 0.047f * scale;
    DrawDial(left + width * 0.24f, top + 0.082f * scale,
             radiusY / safe.aspect, radiusY, speedFraction,
             unit, maximumSpeedText, accent, opacity, scale);
    DrawDial(left + width * 0.76f, top + 0.082f * scale,
             radiusY / safe.aspect, radiusY,
             std::clamp(rpm, 0.0f, 1.0f),
             data.electric ? "POWER" : "RPM",
             data.electric ? "100" : maximumRpmText,
             accent, opacity, scale);
    GRAPHICS::DRAW_RECT(x, top + 0.083f * scale,
                        0.078f * scale, 0.064f * scale,
                        15, 18, 21, opacity, 0);
    Renderer::DrawTextOverlay(
        gearText.c_str(), x, top + 0.052f * scale,
        0.58f * scale, accent.r, accent.g, accent.b,
        opacity, 2, true, true);
    Renderer::DrawTextOverlay(
        data.motorcycle ? "CRUISER" : "TOURING",
        x, top + 0.107f * scale, 0.19f * scale,
        175, 185, 194, opacity, 0, false, true);
  } else {
    Renderer::DrawTextOverlay(
        data.motorcycle ? "BIKE TRACK" : "TRACK BAR",
        left + 0.014f * scale, top + 0.023f * scale,
        0.22f * scale, accent.r, accent.g, accent.b,
        opacity, 0, false, false);
    Renderer::DrawTextOverlay(
        gearText.c_str(), x, top + 0.036f * scale,
        0.94f * scale, accent.r, accent.g, accent.b,
        opacity, 2, true, true);
    Renderer::DrawTextOverlay(
        speedText, left + width - 0.060f * scale,
        top + 0.052f * scale, 0.58f * scale,
        245, 248, 252, opacity, 2, true, true);
    Renderer::DrawTextOverlay(
        unit, left + width - 0.060f * scale,
        top + 0.089f * scale, 0.19f * scale,
        145, 160, 175, opacity, 0, false, true);
  }

  if (style == 1 || style == 2 || style == 3 ||
      style == 5 || style == 7) {
    DrawShiftLights(x, top + (mainHeight - 0.012f) * scale,
                    (data.motorcycle ? 0.105f : 0.125f) * scale,
                    rpm, opacity, scale);
  }

  const float iconY = top + (mainHeight + 0.015f) * scale;
  if (Config::SpeedometerShowIcons) {
    GRAPHICS::DRAW_RECT(x, iconY - 0.012f * scale, width,
                        0.002f * scale, 45, 53, 62, opacity, 0);
    DrawStatusIcons(data, x, iconY, width, accent, opacity, scale);
  }

  if (!Config::SpeedometerDetailed)
    return;

  const float detailTop =
      Config::SpeedometerShowIcons
          ? iconY + 0.022f * scale
          : top + (mainHeight + 0.010f) * scale;
  char conditionLine[160]{};
  sprintf_s(conditionLine,
            "FUEL %02d%%  OIL %02d%%/%02d%%  ENG %02d%%  GBX %02d%%",
            static_cast<int>(std::clamp(data.fuel, 0.0f, 1.0f) * 100.0f),
            static_cast<int>(
                std::clamp(data.oilTemperature, 0.0f, 1.0f) * 100.0f),
            static_cast<int>(
                std::clamp(data.oilLife, 0.0f, 1.0f) * 100.0f),
            static_cast<int>(
                std::clamp(data.engineHealth, 0.0f, 1.0f) * 100.0f),
            static_cast<int>(
                std::clamp(data.gearboxHealth, 0.0f, 1.0f) * 100.0f));
  Renderer::DrawTextOverlay(conditionLine, left + 0.012f * scale,
                            detailTop, 0.215f * scale,
                            185, 198, 210, opacity, 0, false, false);

  char driveLine[160]{};
  sprintf_s(driveLine,
            "CLT %02d%%  BOOST %.1f  THR %02d%%  BRK %02d%%  %.1f KM",
            static_cast<int>(
                std::clamp(data.clutchHeat, 0.0f, 1.0f) * 100.0f),
            std::max(0.0f, data.boost),
            static_cast<int>(
                std::clamp(data.throttle, 0.0f, 1.0f) * 100.0f),
            static_cast<int>(
                std::clamp(data.brake, 0.0f, 1.0f) * 100.0f),
            std::max(0.0f, data.odometerKm));
  Renderer::DrawTextOverlay(driveLine, left + 0.012f * scale,
                            detailTop + 0.022f * scale,
                            0.215f * scale, 150, 166, 180, opacity,
                            0, false, false);
}

} // namespace Speedometer
