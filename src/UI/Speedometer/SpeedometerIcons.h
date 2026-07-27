#pragma once

namespace SpeedometerIcons {

struct Colour {
  int r;
  int g;
  int b;
};

void DrawPower(float x, float y, bool on, const Colour &accent,
               int opacity, float scale);
void DrawStateBadge(const char *label, float x, float y, bool enabled,
                    bool active,
                    const Colour &onColour, int opacity, float scale);
void DrawWarningTriangle(float x, float y, bool on, int opacity, float scale);

} // namespace SpeedometerIcons
