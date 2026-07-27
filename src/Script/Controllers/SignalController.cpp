// =============================================================================
// SignalController.cpp — Turn signal and hazard light logic.
// Extracted from MainScript.cpp baris 481–509. Auto-cancel via steering retained.
// =============================================================================
#include "SignalController.h"

#include "../../Core/Config.h"
#include "../../Core/InputHandler.h"
#include "../../Core/ModLogger.h"
#include "../../../sdk/inc/natives.h"

void SignalController::Reset() {
    m_activeSignal = 0;
}

void SignalController::Update(Vehicle veh) {
    // ── Auto-cancel via steering ──────────────────────────────────────────
    if (Config::SignalAutoCancelSteer &&
        (m_activeSignal == 1 || m_activeSignal == 2)) {
        const float rawSteer = InputHandler::GetRawSteer();
        if ((m_activeSignal == 1 && rawSteer > 0.01f) ||
            (m_activeSignal == 2 && rawSteer < -0.01f)) {
            m_activeSignal = 0;
            VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(veh, 0, FALSE);
            VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(veh, 1, FALSE);
            LOG_DEBUG(Sig, "Signal auto-cancelled via steering");
        }
    }

    // ── Toggle hazard ─────────────────────────────────────────────────────
    if (InputHandler::IsSignalHazardJustPressed()) {
        m_activeSignal = (m_activeSignal == 3) ? 0 : 3;
        VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(veh, 0, m_activeSignal == 3);
        VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(veh, 1, m_activeSignal == 3);
        LOG_DEBUG(Sig, "Signal HAZARD -> %d", m_activeSignal);
    } else if (InputHandler::IsSignalLeftJustPressed()) {
        m_activeSignal = (m_activeSignal == 1) ? 0 : 1;
        VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(veh, 0, FALSE);
        VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(veh, 1, m_activeSignal == 1);
        LOG_DEBUG(Sig, "Signal LEFT -> %d", m_activeSignal);
    } else if (InputHandler::IsSignalRightJustPressed()) {
        m_activeSignal = (m_activeSignal == 2) ? 0 : 2;
        VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(veh, 1, FALSE);
        VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(veh, 0, m_activeSignal == 2);
        LOG_DEBUG(Sig, "Signal RIGHT -> %d", m_activeSignal);
    }
}
