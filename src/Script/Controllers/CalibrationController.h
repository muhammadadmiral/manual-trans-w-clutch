// =============================================================================
// CalibrationController.h — AOB calibration path and layout validation.
// =============================================================================
#pragma once

#include "../../Vehicle/VehicleData.h"
#include <Windows.h>

using Vehicle = int;

class CalibrationController {
public:
    // Per-frame update while calibration is in progress.
    // Returns true if calibration just succeeded this frame.
    bool Update(Vehicle veh, bool isEngineOn, float smoothThrottle,
                int maxGear, VehicleData& data);

    bool IsLayoutValid()   const { return m_layoutValid; }
    bool IsLayoutChecked() const { return m_layoutChecked; }

    void ResetLayout();
    void CheckLayout(VehicleData& data, int maxGear);

private:
    CalibrationState m_lastState    = CalibrationState::None;
    bool m_layoutValid              = false;
    bool m_layoutChecked            = false;

    void DrawHUD(CalibrationState state, float smoothedThrottle);
};
