// =============================================================================
// CalibrationController.cpp — AOB calibration path and HUD rendering.
// Extracted from MainScript.cpp baris 98–140 (DrawCalibrationHUD) and
// baris 511–557 (calibration loop), plus layout validation at baris 372–390.
// =============================================================================
#include "CalibrationController.h"

#include "../../Core/Config.h"
#include "../../Core/ModLogger.h"
#include "../../Core/Renderer.h"
#include "../../../sdk/inc/natives.h"

#include <algorithm>
#include <string>

extern HMODULE g_pluginModule;

void CalibrationController::ResetLayout() {
    m_layoutValid   = false;
    m_layoutChecked = false;
    m_lastState     = CalibrationState::None;
}

void CalibrationController::CheckLayout(VehicleData& data, int maxGear) {
    if (!VehicleData::IsInitialized() || m_layoutChecked)
        return;

    m_layoutValid   = data.HasPlausibleLayout(maxGear > 0 ? maxGear : 6);
    m_layoutChecked = true;

    if (!m_layoutValid) {
        LOG_ERROR(Memory,
                  "Resolved layout rejected on live vehicle: src=%s "
                  "G=0x%X N=0x%X TG=0x%X RPM=0x%X",
                  VehicleData::GetOffsetSourceName(),
                  VehicleData::GetResolvedOffsets().Gear,
                  VehicleData::GetResolvedOffsets().NextGear,
                  VehicleData::GetResolvedOffsets().TopGear,
                  VehicleData::GetResolvedOffsets().RPM);
        Renderer::ShowNotification(
            "~r~Offset invalid:~w~ calibration aman dimulai ulang");
        VehicleData::ResetCalibration();
        m_lastState = CalibrationState::None;
    }
}

bool CalibrationController::Update(Vehicle veh, bool isEngineOn,
                                    float smoothThrottle, int maxGear,
                                    VehicleData& data) {
    (void)data;
    const bool isRevving = smoothThrottle > 0.5f;

    VehicleData::UpdateCalibration(g_pluginModule, veh, isEngineOn,
                                   isRevving, maxGear);

    const CalibrationState state = VehicleData::GetCalibrationState();

    if (state != m_lastState) {
        LOG_INFO(Calib, "State %d -> %d | candidates=%zu",
                 static_cast<int>(m_lastState), static_cast<int>(state),
                 VehicleData::GetCalibrationCandidateCount());

        if (state == CalibrationState::Failed) {
            const std::string& reason = VehicleData::GetLastFailureReason();
            LOG_ERROR(Calib, "Calibration FAILED: %s", reason.c_str());
            Renderer::ShowNotification(
                ("~r~Calibration Failed:~w~ " + reason).c_str());
        }

        m_lastState = state;
    }

    if (VehicleData::IsInitialized()) {
        // The current VehicleData object was bound before calibration filled
        // the offsets. Validate a freshly bound object on the next frame.
        const VehicleOffsets& off = VehicleData::GetResolvedOffsets();
        LOG_INFO(Calib,
                 "Calibration done — RPM=0x%X CLT=0x%X G=0x%X N=0x%X TG=0x%X",
                 off.RPM, off.Clutch, off.Gear, off.NextGear, off.TopGear);
        m_layoutValid = false;
        m_layoutChecked = false;
        Renderer::ShowNotification(
            "~g~Calibration complete! Manual transmission active.");
        return false;
    }

    DrawHUD(state, smoothThrottle);
    LOG_DEBUG_T(Calib, 1000,
                "Calib in progress: state=%d candidates=%zu throttle=%.2f "
                "revving=%d",
                static_cast<int>(state),
                VehicleData::GetCalibrationCandidateCount(), smoothThrottle,
                static_cast<int>(isRevving));
    return false;
}

void CalibrationController::DrawHUD(CalibrationState state,
                                     float smoothedThrottle) {
    std::string msg = "Calibration: ";
    switch (state) {
    case CalibrationState::Failed:
        msg += "~r~FAILED~w~ — check melar-transmission.log";
        break;
    case CalibrationState::WaitingForEngineOff:
        msg += "Turn engine OFF (press " +
               std::string(1, static_cast<char>(Config::KeyEngine)) + ")";
        break;
    case CalibrationState::WaitingForEngineOn:
        msg += "Turn engine ON and let it idle";
        break;
    case CalibrationState::ScanningEngineOff:
        msg += "Waiting for RPM to reach 0...";
        break;
    case CalibrationState::ScanningEngineOn:
        msg += "Sampling idle RPM...";
        break;
    case CalibrationState::WaitingForRev:
        msg += "~g~Hold throttle (W) to rev engine";
        break;
    case CalibrationState::ScanningRev:
        msg += "Sampling rev RPM...";
        break;
    case CalibrationState::Done:
        msg += "~g~SUCCESS! Offsets saved.";
        break;
    default:
        msg += "Scanning... (" +
               std::to_string(VehicleData::GetCalibrationCandidateCount()) +
               " candidates)";
        break;
    }
    Renderer::DrawTextOverlay(msg.c_str(), 0.5f, 0.10f, 0.60f);

    char dbg[160]{};
    sprintf_s(dbg, "[debug] W=%s throttle=%.2f state=%d candidates=%zu",
              (GetAsyncKeyState(0x57) & 0x8000) ? "DOWN" : "up",
              smoothedThrottle, static_cast<int>(state),
              VehicleData::GetCalibrationCandidateCount());
    Renderer::DrawTextOverlay(dbg, 0.5f, 0.15f, 0.34f);
}
