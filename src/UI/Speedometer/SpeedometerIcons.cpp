#include "SpeedometerIcons.h"

#include "../../Core/Renderer.h"
#include "../../../sdk/inc/natives.h"

#include <cmath>

namespace SpeedometerIcons {

void DrawPower(float x, float y, bool on, const Colour &accent,
               int opacity, float scale) {
  const int r = on ? accent.r : 68;
  const int g = on ? accent.g : 72;
  const int b = on ? accent.b : 78;
  const int alpha = on ? opacity : opacity / 2;
  constexpr float kPi = 3.14159265f;
  for (int i = 0; i < 14; ++i) {
    const float angle =
        -0.80f * kPi + static_cast<float>(i) / 13.0f * 1.60f * kPi;
    GRAPHICS::DRAW_RECT(
        x + std::cos(angle) * 0.0080f * scale,
        y + std::sin(angle) * 0.0115f * scale,
        0.0018f * scale, 0.0026f * scale, r, g, b, alpha, 0);
  }
  GRAPHICS::DRAW_RECT(x, y - 0.0070f * scale,
                      0.0020f * scale, 0.0130f * scale,
                      r, g, b, alpha, 0);
}

void DrawStateBadge(const char *label, float x, float y, bool enabled,
                    bool active,
                    const Colour &onColour, int opacity, float scale) {
  const float width = 0.030f * scale;
  const float height = 0.018f * scale;
  const int r = active ? onColour.r : (enabled ? 44 : 28);
  const int g = active ? onColour.g : (enabled ? 51 : 31);
  const int b = active ? onColour.b : (enabled ? 59 : 35);
  GRAPHICS::DRAW_RECT(x, y, width, height, r, g, b,
                      active ? opacity : opacity / 2, 0);
  if (!active) {
    GRAPHICS::DRAW_RECT(x, y - height * 0.5f, width,
                        0.0012f * scale,
                        enabled ? 98 : 58, enabled ? 108 : 62,
                        enabled ? 118 : 68, opacity / 2, 0);
    GRAPHICS::DRAW_RECT(x, y + height * 0.5f, width,
                        0.0012f * scale,
                        enabled ? 98 : 58, enabled ? 108 : 62,
                        enabled ? 118 : 68, opacity / 2, 0);
  }
  if (!enabled) {
    GRAPHICS::DRAW_RECT(x, y, width * 0.72f,
                        0.0015f * scale, 115, 52, 52,
                        opacity / 2, 0);
  }
  Renderer::DrawTextOverlay(
      label, x, y - 0.0090f * scale, 0.185f * scale,
      active ? 8 : (enabled ? 165 : 92),
      active ? 12 : (enabled ? 174 : 96),
      active ? 16 : (enabled ? 184 : 102),
      active ? opacity : opacity / 2, 0, false, true);
}

void DrawWarningTriangle(float x, float y, bool on, int opacity, float scale) {
  const int r = on ? 255 : 62;
  const int g = on ? 72 : 66;
  const int b = on ? 48 : 72;
  const int alpha = on ? opacity : opacity / 3;
  for (int row = 0; row < 7; ++row) {
    const float fraction = static_cast<float>(row + 1) / 7.0f;
    const float width = 0.017f * scale * fraction;
    GRAPHICS::DRAW_RECT(x, y - 0.008f * scale +
                               row * 0.0022f * scale,
                        width, 0.0016f * scale, r, g, b, alpha, 0);
  }
  Renderer::DrawTextOverlay(
      "!", x, y - 0.0095f * scale, 0.19f * scale,
      on ? 255 : 125, on ? 245 : 130, on ? 235 : 138,
      on ? opacity : opacity / 2, 0, false, true);
}

} // namespace SpeedometerIcons
