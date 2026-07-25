// Orkestrator per-frame. Rumus fisika jangan ditaruh di sini, cuy.
#define NOMINMAX
#include "MainScript.h"

#include "../../sdk/inc/main.h"
#include "../../sdk/inc/natives.h"

#include "../Core/Config.h"
#include "../Core/InputHandler.h"
#include "../Core/Menu.h"
#include "../Core/ModLogger.h"
#include "../Core/Renderer.h"
#include "../Memory/GearboxPatches.h"
#include "../Vehicle/Brakes/BrakeSystem.h"
#include "../Vehicle/Brakes/ParkingBrake.h"
#include "../Vehicle/Clutch/ClutchSystem.h"
#include "../Vehicle/Engine/EngineModel.h"
#include "../Vehicle/Engine/FuelSystem.h"
#include "../Vehicle/Engine/LaunchControl.h"
#include "../Vehicle/Engine/PedalModel.h"
#include "../Vehicle/Engine/TractionControl.h"
#include "../Vehicle/Engine/TurboSystem.h"
#include "../Vehicle/Gears/GearboxSystem.h"
#include "../Vehicle/Gears/AutomaticGearbox.h"
#include "../Vehicle/Gears/GearLogic.h"
#include "../Vehicle/LightsLogic.h"
#include "../Vehicle/TelemetryLogger.h"
#include "../Vehicle/VehicleData.h"
#include "../Vehicle/VehicleProfile.h"
#include "../Vehicle/VehicleUpgrades.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

extern HMODULE g_pluginModule;

// =============================================================================
// Vehicle validation helpers (anonymous — not part of the public API)
// =============================================================================
namespace {

bool IsModelExcluded(int vehicleClass) {
  for (const int excl : Config::ExcludedVehicleClasses)
    if (excl == vehicleClass)
      return true;
  return false;
}

// Returns true if the vehicle model should be controlled by this mod.
// Slow path — checks many native predicates.
bool ComputeIsValidVehicle(Vehicle vehicle) {
  if (!vehicle || !ENTITY::DOES_ENTITY_EXIST(vehicle))
    return false;
  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
  if (VEHICLE::IS_THIS_MODEL_A_PLANE(model) ||
      VEHICLE::IS_THIS_MODEL_A_HELI(model) ||
      VEHICLE::IS_THIS_MODEL_A_BOAT(model) ||
      VEHICLE::IS_THIS_MODEL_A_JETSKI(model) ||
      VEHICLE::IS_THIS_MODEL_A_TRAIN(model) ||
      VEHICLE::IS_THIS_MODEL_A_BICYCLE(model))
    return false;
  if (!Config::AllowQuadbikes && VEHICLE::IS_THIS_MODEL_A_QUADBIKE(model))
    return false;
  if (IsModelExcluded(VEHICLE::GET_VEHICLE_CLASS(vehicle)))
    return false;
  return true;
}

// Cached per-model result so we don't call natives every frame.
bool IsValidVehicle(Vehicle vehicle) {
  if (!vehicle || !ENTITY::DOES_ENTITY_EXIST(vehicle))
    return false;
  static std::unordered_map<Hash, bool> s_cache;
  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
  const auto it = s_cache.find(model);
  if (it != s_cache.end())
    return it->second;
  const bool v = ComputeIsValidVehicle(vehicle);
  if (v)
    s_cache.emplace(model, true);
  return v;
}

bool IsPlayerDriving(Ped playerPed, Vehicle vehicle) {
  return VEHICLE::GET_PED_IN_VEHICLE_SEAT(vehicle, -1, 0) == playerPed;
}

// ── Calibration HUD
// ───────────────────────────────────────────────────────────
void DrawCalibrationHUD(CalibrationState state, float smoothedThrottle) {
  std::string msg = "Calibration: ";
  switch (state) {
  case CalibrationState::Failed:
    msg += "~r~FAILED~w~ — check manual-trans.log";
    break;
  case CalibrationState::WaitingForEngineOff:
    msg += "Turn engine OFF (press " + std::string(1, (char)Config::KeyEngine) +
           ")";
    break;
  case CalibrationState::WaitingForEngineOn:
    msg += "Turn engine ON and let it idle";
    break;
  case CalibrationState::ScanningEngineOff:
    msg += "Waiting for RPM to reach 0…";
    break;
  case CalibrationState::ScanningEngineOn:
    msg += "Sampling idle RPM…";
    break;
  case CalibrationState::WaitingForRev:
    msg += "~g~Hold throttle (W) to rev engine";
    break;
  case CalibrationState::ScanningRev:
    msg += "Sampling rev RPM…";
    break;
  case CalibrationState::Done:
    msg += "~g~SUCCESS! Offsets saved.";
    break;
  default:
    msg += "Scanning… (" +
           std::to_string(VehicleData::GetCalibrationCandidateCount()) +
           " candidates)";
    break;
  }
  Renderer::DrawTextOverlay(msg.c_str(), 0.5f, 0.10f, 0.60f);

  char dbg[160]{};
  sprintf_s(dbg, "[debug] W=%s throttle=%.2f state=%d candidates=%zu",
            (GetAsyncKeyState(0x57) & 0x8000) ? "DOWN" : "up", smoothedThrottle,
            static_cast<int>(state),
            VehicleData::GetCalibrationCandidateCount());
  Renderer::DrawTextOverlay(dbg, 0.5f, 0.15f, 0.34f);
}

} // namespace

// =============================================================================
// ScriptMain — the ScriptHookV game-loop thread
// =============================================================================
void ScriptMain() {
  LOG_INFO(Script, "ScriptMain started — waiting for player ped...");

  // ── Phase 1: wait for the player to spawn ─────────────────────────────────
  while (true) {
    scriptWait(1000);
    const Ped p = PLAYER::PLAYER_PED_ID();
    if (ENTITY::DOES_ENTITY_EXIST(p) && !PED::IS_PED_INJURED(p))
      break;
  }
  LOG_INFO(Script, "Player ped ready. Initializing VehicleData...");

  // ── Phase 2: resolve CVehicle offsets ─────────────────────────────────────
  if (!VehicleData::Initialize(g_pluginModule)) {
    const std::string reason = VehicleData::GetLastFailureReason();
    LOG_FATAL(Script, "VehicleData::Initialize returned false: %s",
              reason.c_str());
    Renderer::ShowNotification(("~r~Manual trans disabled: " + reason).c_str());
    return;
  }
  LOG_INFO(Script, "VehicleData::Initialize OK. Reading config...");

  Config::ReadConfig(g_pluginModule);
  ModLogger::SetMinLevel(Config::DebugOverlay ? ModLogger::Level::Verbose
                                              : ModLogger::Level::Info);
  LOG_INFO(Script, "Config loaded. Verbose logging: %s",
           Config::DebugOverlay ? "ON" : "off");

  // ── Per-session state ─────────────────────────────────────────────────────
  bool notificationShown = false;
  bool patchFailureShown = false;
  Vehicle activeVehicle = 0;
  bool activeLayoutValid = false;
  bool activeLayoutChecked = false;
  bool isEngineOn = true;
  bool engineStarting = false;
  ULONGLONG engineStartTick = 0;
  int grindWarningTimer = 0;
  int manualGear = 0;
  int activeTransmissionMode = -1;
  int activeSignal = 0;
  ULONGLONG vehicleEnterTick = 0;
  ULONGLONG automaticClutchUntil = 0;

  CalibrationState lastCalibState = CalibrationState::None;

  // ── Main loop ─────────────────────────────────────────────────────────────
  while (true) {
    scriptWait(0);

    // Force re-calibrate from menu
    if (Config::ForceRecalibrate) {
      Config::ForceRecalibrate = false;
      LOG_INFO(Calib, "ForceRecalibrate flag set — resetting calibration");
      VehicleData::ResetCalibration();
      lastCalibState = CalibrationState::None;
      activeLayoutChecked = false;
    }

    Menu::Update();
    const Ped playerPed = PLAYER::PLAYER_PED_ID();

    // ── Not in a vehicle ──────────────────────────────────────────────────
    if (!PED::IS_PED_IN_ANY_VEHICLE(playerPed, FALSE)) {
      GearboxPatches::SetActive(false);
      if (activeVehicle) {
        if (ENTITY::DOES_ENTITY_EXIST(activeVehicle)) {
          VehicleData previousData(activeVehicle);
          EngineModel::RestoreRuntimeDriveForce(previousData);
          previousData.SetClutch(1.0f);
          VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(activeVehicle, 1.0f);
        }
        LOG_INFO(Script, "Player exited vehicle %d — resetting session state",
                 activeVehicle);
      }
      activeVehicle = 0;
      activeLayoutValid = false;
      activeLayoutChecked = false;
      activeSignal = 0;
      InputHandler::ResetEdges();
      Menu::Draw();
      continue;
    }

    const Vehicle vehicle = PED::GET_VEHICLE_PED_IS_IN(playerPed, FALSE);
    if (!IsValidVehicle(vehicle) || !IsPlayerDriving(playerPed, vehicle)) {
      GearboxPatches::SetActive(false);
      if (activeVehicle && ENTITY::DOES_ENTITY_EXIST(activeVehicle)) {
        VehicleData previousData(activeVehicle);
        EngineModel::RestoreRuntimeDriveForce(previousData);
        previousData.SetClutch(1.0f);
        VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(activeVehicle, 1.0f);
      }
      activeVehicle = 0;
      activeLayoutValid = false;
      activeLayoutChecked = false;
      activeSignal = 0;
      InputHandler::ResetEdges();
      Menu::Draw();
      continue;
    }

    // ── One-time "mod active" notification ────────────────────────────────
    if (!notificationShown && VehicleData::IsInitialized()) {
      notificationShown = true;
      const VehicleOffsets &off = VehicleData::GetResolvedOffsets();
      const std::string bv = VehicleData::GetGameBuildVersion();
      char notify[256]{};
      sprintf_s(notify, "Manual trans r13: %s | build %s | G:%X N:%X RPM:%X CLT:%X",
                VehicleData::GetOffsetSourceName(),
                bv.empty() ? "?" : bv.c_str(), off.Gear, off.NextGear, off.RPM,
                off.Clutch);
      Renderer::ShowNotification(notify);
      LOG_INFO(
          Init, "Mod active — src=%s build=%s G=0x%X N=0x%X RPM=0x%X "
                "CLT=0x%X THR=0x%X HP=0x%X WHL=0x%X",
          VehicleData::GetOffsetSourceName(), bv.empty() ? "?" : bv.c_str(),
          off.Gear, off.NextGear, off.RPM, off.Clutch, off.Throttle,
          off.HandlingPtr, off.WheelsPtr);
    }

    const int maxGear = VEHICLE::_GET_VEHICLE_MAX_DRIVE_GEAR_COUNT(vehicle);
    VehicleData data(vehicle);
    const bool actualEngineOn =
        (VEHICLE::GET_IS_VEHICLE_ENGINE_RUNNING(vehicle) != 0);
    const VehicleProfile::Drivetrain vehicleProfile =
        VehicleProfile::Detect(vehicle);

    // ── Vehicle change ────────────────────────────────────────────────────
    if (vehicle != activeVehicle) {
      if (activeVehicle && ENTITY::DOES_ENTITY_EXIST(activeVehicle)) {
        VehicleData previousData(activeVehicle);
        EngineModel::RestoreRuntimeDriveForce(previousData);
        previousData.SetClutch(1.0f);
        VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(activeVehicle, 1.0f);
      }
      LOG_INFO(Script, "Entered vehicle handle=%d maxGear=%d", vehicle,
               maxGear);
      activeVehicle = vehicle;
      activeLayoutValid = false;
      activeLayoutChecked = false;

      isEngineOn = Config::RequireColdStart ? false : actualEngineOn;
      engineStarting = false;
      engineStartTick = 0;
      automaticClutchUntil = 0;
      if (Config::RequireColdStart) {
        VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
        LOG_INFO(Script,
                 "Cold start required — engine forced OFF for vehicle %d",
                 vehicle);
      }

      GearLogic::Reset(0);
      AutomaticGearbox::Reset();
      GearboxSystem::Reset();
      ClutchSystem::Reset();
      EngineModel::Reset();
      VehicleUpgrades::Reset();
      VehicleUpgrades::Initialize(vehicle);
      LaunchControl::Reset();
      PedalModel::Reset();
      BrakeSystem::Reset();
      ParkingBrake::Reset();
      FuelSystem::Reset();
      TractionControl::Reset();
      TurboSystem::Reset();
      TurboSystem::InitializeForVehicle(vehicle);
      VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(vehicle, 1.0f);
      LOG_INFO(
          Script,
          "Vehicle profile=%s engineMod=%d/%d transmissionMod=%d/%d "
          "race=%d quick=%d power=%d",
          VehicleProfile::GetName(vehicleProfile),
          VehicleUpgrades::GetState().engineLevel,
          VehicleUpgrades::GetState().engineMaxLevel,
          VehicleUpgrades::GetState().transmissionLevel,
          VehicleUpgrades::GetState().transmissionMaxLevel,
          VehicleUpgrades::GetState().raceTransmission ? 1 : 0,
          VehicleUpgrades::GetState().quickshifter ? 1 : 0,
          VehicleUpgrades::GetState().powershifter ? 1 : 0);

      if (TelemetryLogger::IsLogging()) {
        TelemetryLogger::StopSession();
        LOG_INFO(Script, "Telemetry session stopped to prevent disk bloat.");
      }

      vehicleEnterTick = GetTickCount64();
      activeTransmissionMode = -1;
      activeSignal = 0;
    }

    // Skip a short grace period while vehicle physics initializes
    if (GetTickCount64() - vehicleEnterTick < 500) {
      Menu::Draw();
      continue;
    }

    if (VehicleData::IsInitialized() && !activeLayoutChecked) {
      activeLayoutValid =
          data.HasPlausibleLayout(maxGear > 0 ? maxGear : 6);
      activeLayoutChecked = true;
      if (!activeLayoutValid) {
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
        lastCalibState = CalibrationState::None;
      }
    }

    InputHandler::Update(manualGear);

    // ── Engine toggle key ─────────────────────────────────────────────────
    if (InputHandler::IsEngineJustPressed()) {
      bool canStart = true;
      if (!isEngineOn && VehicleData::IsInitialized() &&
          Config::StarterInterlock && Config::TransmissionMode != 0) {
        const bool automaticStart =
            VehicleProfile::ForcesAutomatic(vehicleProfile) ||
            Config::TransmissionMode == 1;
        if (automaticStart) {
          const auto selector = AutomaticGearbox::GetSelector();
          const bool safeSelector =
              vehicleProfile == VehicleProfile::Drivetrain::ScooterCVT ||
              selector == AutomaticGearbox::Selector::Park ||
              selector == AutomaticGearbox::Selector::Neutral;
          const bool brakeReady =
              !Config::AutomaticStartRequiresBrake ||
              InputHandler::GetSmoothedBrake() >= 0.25f;
          canStart = safeSelector && brakeReady;
        } else {
          canStart = manualGear == 0 || InputHandler::IsClutchDown();
        }
      }

      if (canStart) {
        if (isEngineOn || engineStarting) {
          isEngineOn = false;
          engineStarting = false;
          VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
          LOG_INFO(Script, "Engine key -> OFF");
        } else {
          if (vehicleProfile == VehicleProfile::Drivetrain::Electric) {
            isEngineOn = true;
            VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, TRUE, TRUE, TRUE);
            LOG_INFO(Script, "EV power -> READY");
          } else {
            engineStarting = true;
            engineStartTick = GetTickCount64();
            VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, TRUE, FALSE, TRUE);
            LOG_INFO(Script, "Engine key -> STARTING");
          }
        }
      } else {
        Renderer::ShowNotification(
            "~r~Starter interlock:~w~ clutch / brake dan posisi gear belum aman");
      }
    } else if (engineStarting) {
      const ULONGLONG crankMs = GetTickCount64() - engineStartTick;
      if (actualEngineOn && crankMs >= 450) {
        engineStarting = false;
        isEngineOn = true;
        LOG_INFO(Script, "Starter completed in %llums", crankMs);
      } else if (crankMs > 2500) {
        engineStarting = false;
        isEngineOn = actualEngineOn;
        LOG_WARN(Script, "Starter timeout actual=%d",
                 actualEngineOn ? 1 : 0);
      }
    } else if (!isEngineOn && actualEngineOn) {
      // Game AI turned it back on — force our state.
      VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
      LOG_DEBUG(Script,
                "Engine mismatch corrected: we=OFF game=ON → forcing OFF");
    } else if (isEngineOn && !actualEngineOn) {
      const float engineHealth =
          VEHICLE::GET_VEHICLE_ENGINE_HEALTH(vehicle);
      if (!VEHICLE::IS_VEHICLE_DRIVEABLE(vehicle, TRUE) ||
          engineHealth <= 0.0f) {
        isEngineOn = false;
        LOG_WARN(Script, "Engine unavailable health=%.1f", engineHealth);
      } else {
        // Flag engine native kadang drop sesaat saat downshift ekstrem.
        VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, TRUE, TRUE, TRUE);
      }
    }

    // ── Turn signals ──────────────────────────────────────────────────────
    if (Config::SignalAutoCancelSteer &&
        (activeSignal == 1 || activeSignal == 2)) {
      const float rawSteer = InputHandler::GetRawSteer();
      if ((activeSignal == 1 && rawSteer > 0.01f) ||
          (activeSignal == 2 && rawSteer < -0.01f)) {
        activeSignal = 0;
        VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(vehicle, 0, FALSE);
        VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(vehicle, 1, FALSE);
        LOG_DEBUG(Sig, "Signal auto-cancelled via steering");
      }
    }

    if (InputHandler::IsSignalHazardJustPressed()) {
      activeSignal = (activeSignal == 3) ? 0 : 3;
      VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(vehicle, 0, activeSignal == 3);
      VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(vehicle, 1, activeSignal == 3);
      LOG_DEBUG(Sig, "Signal HAZARD → %d", activeSignal);
    } else if (InputHandler::IsSignalLeftJustPressed()) {
      activeSignal = (activeSignal == 1) ? 0 : 1;
      VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(vehicle, 0, FALSE);
      VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(vehicle, 1, activeSignal == 1);
      LOG_DEBUG(Sig, "Signal LEFT → %d", activeSignal);
    } else if (InputHandler::IsSignalRightJustPressed()) {
      activeSignal = (activeSignal == 2) ? 0 : 2;
      VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(vehicle, 1, FALSE);
      VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(vehicle, 0, activeSignal == 2);
      LOG_DEBUG(Sig, "Signal RIGHT → %d", activeSignal);
    }

    // ── Calibration path (while not initialized) ──────────────────────────
    if (!VehicleData::IsInitialized()) {
      const float smoothThrottle = InputHandler::GetSmoothedThrottle();
      const bool isRevving = smoothThrottle > 0.5f;

      VehicleData::UpdateCalibration(g_pluginModule, vehicle, isEngineOn,
                                     isRevving, maxGear);

      const CalibrationState state = VehicleData::GetCalibrationState();

      if (state != lastCalibState) {
        LOG_INFO(Calib, "State %d → %d | candidates=%zu",
                 static_cast<int>(lastCalibState), static_cast<int>(state),
                 VehicleData::GetCalibrationCandidateCount());

        if (state == CalibrationState::Failed) {
          const std::string &reason = VehicleData::GetLastFailureReason();
          LOG_ERROR(Calib, "Calibration FAILED: %s", reason.c_str());
          Renderer::ShowNotification(
              ("~r~Calibration Failed:~w~ " + reason).c_str());
        }

        lastCalibState = state;
      }

      if (VehicleData::IsInitialized()) {
        // Just succeeded!
        const VehicleOffsets &off = VehicleData::GetResolvedOffsets();
        LOG_INFO(Calib,
                 "Calibration done — RPM=0x%X CLT=0x%X G=0x%X N=0x%X TG=0x%X",
                 off.RPM, off.Clutch, off.Gear, off.NextGear, off.TopGear);
        activeLayoutValid = data.HasPlausibleLayout(maxGear > 0 ? maxGear : 6);
        activeLayoutChecked = true;
        Renderer::ShowNotification(
            "~g~Calibration complete! Manual transmission active.");
      } else {
        DrawCalibrationHUD(state, smoothThrottle);
        LOG_DEBUG_T(Calib, 1000,
                    "Calib in progress: state=%d candidates=%zu throttle=%.2f "
                    "revving=%d",
                    static_cast<int>(state),
                    VehicleData::GetCalibrationCandidateCount(), smoothThrottle,
                    static_cast<int>(isRevving));
      }
      Menu::Draw();
      continue;
    }

    // ── Layout validation ─────────────────────────────────────────────────
    if (!activeLayoutValid || !data.IsValid()) {
      GearboxPatches::SetActive(false);
      LOG_WARN_T(Memory, 2000,
                 "Layout invalid — skipping frame (valid=%d dataValid=%d)",
                 static_cast<int>(activeLayoutValid),
                 static_cast<int>(data.IsValid()));
      Menu::Draw();
      continue;
    }

    // Removed mid-session plausibility check to prevent false positives when spawning new vehicles
    // Deluxo hover-mode: skip manual trans logic when hovering
    if (data.GetHoverTransformRatioLerp() > 0.0f) {
      GearboxPatches::SetActive(false);
      LOG_DEBUG_T(Script, 3000, "Deluxo hover active — skipping");
      Menu::Draw();
      continue;
    }

    const int requestedMode = std::clamp(Config::TransmissionMode, 0, 2);
    const bool electricVehicle =
        vehicleProfile == VehicleProfile::Drivetrain::Electric;
    const bool scooterVehicle =
        vehicleProfile == VehicleProfile::Drivetrain::ScooterCVT;
    const int transmissionMode =
        VehicleProfile::ForcesAutomatic(vehicleProfile) ? 1 : requestedMode;
    const bool nativePatchReady =
        GearboxPatches::SetActive(transmissionMode != 0);
    if (transmissionMode != 0 && !nativePatchReady && !patchFailureShown) {
      patchFailureShown = true;
      const std::string reason = GearboxPatches::GetFailureReason();
      Renderer::ShowNotification(
          ("~r~Native gearbox patch gagal:~w~ " +
           (reason.empty() ? std::string("signature Enhanced tidak cocok")
                           : reason))
              .c_str());
    }
    if (transmissionMode != activeTransmissionMode) {
      if (activeTransmissionMode != -1)
        EngineModel::RestoreRuntimeDriveForce(data);
      if (activeTransmissionMode == 1)
        VEHICLE::SET_VEHICLE_HANDBRAKE(vehicle, FALSE);
      GearLogic::Reset(0);
      AutomaticGearbox::Reset(
          scooterVehicle ? AutomaticGearbox::Selector::Drive
                         : AutomaticGearbox::Selector::Park);
      GearboxSystem::Reset();
      ClutchSystem::Reset();
      EngineModel::Reset();
      PedalModel::Reset();
      manualGear = 0;
      automaticClutchUntil = 0;
      data.SetClutch(1.0f);
      VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(vehicle, 1.0f);
      activeTransmissionMode = transmissionMode;

      if (electricVehicle && requestedMode == 2) {
        Renderer::ShowNotification(
            "~y~EV detected:~w~ manual locked, automatic active");
      } else if (scooterVehicle && requestedMode != 1) {
        Renderer::ShowNotification(
            "~y~Scooter CVT:~w~ gas/rem only, clutch dan manual nonaktif");
      } else {
        Renderer::ShowNotification(
            transmissionMode == 0
                ? "Transmission mod: ~c~OFF"
                : (transmissionMode == 1
                       ? "Transmission: ~b~AUTOMATIC P-R-N-D-S-L2-L1"
                       : "Transmission: ~g~MANUAL SEQUENTIAL"));
      }
      LOG_INFO(Gear, "Transmission mode=%d requested=%d profile=%s",
               transmissionMode, requestedMode,
               VehicleProfile::GetName(vehicleProfile));
    }

    if (transmissionMode == 0) {
      GearboxPatches::SetActive(false);
      EngineModel::RestoreRuntimeDriveForce(data);
      data.SetClutch(1.0f);
      VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(vehicle, 1.0f);
      Menu::Draw();
      continue;
    }

    // ── Per-frame values ──────────────────────────────────────────────────
    const float vehicleSpeed = ENTITY::GET_ENTITY_SPEED(vehicle);
    const float speedKmH = vehicleSpeed * 3.6f;
    const float forwardSpeed = ENTITY::GET_ENTITY_SPEED_VECTOR(vehicle, TRUE).y;
    // Tombol clutch harus putus di frame pertama. Attack/release tetap kepakai
    // buat travel setelah tombol dilepas.
    const bool automaticMode = transmissionMode == 1;
    const bool motorcycleAutoClutch =
        transmissionMode == 2 &&
        vehicleProfile == VehicleProfile::Drivetrain::MotorcycleSequential;
    const bool shiftUpPressed = InputHandler::IsShiftUpJustPressed();
    const bool shiftDownPressed = InputHandler::IsShiftDownJustPressed();
    const bool nativeQuickshift =
        VehicleUpgrades::GetState().quickshifter && shiftUpPressed &&
        manualGear > 0;
    if (motorcycleAutoClutch && (shiftUpPressed || shiftDownPressed) &&
        !nativeQuickshift) {
      const ULONGLONG clutchWindow =
          VehicleUpgrades::GetState().transmissionStage > 0.0f ? 110 : 170;
      automaticClutchUntil = GetTickCount64() + clutchWindow;
    }
    const bool automaticClutchOpen =
        motorcycleAutoClutch && GetTickCount64() < automaticClutchUntil;
    const float clutch =
        automaticClutchOpen
            ? 1.0f
            : (!automaticMode && InputHandler::IsClutchDown()
                   ? 1.0f
                   : (!automaticMode ? InputHandler::GetSmoothedClutch()
                                     : 0.0f));
    const float rawThrottle = InputHandler::GetSmoothedThrottle();
    const float rawBrake = InputHandler::GetSmoothedBrake();
    const float rpm = data.GetRPM();

    // ── Subsystem updates ─────────────────────────────────────────────────
    float simulatedClutch =
        automaticMode
            ? AutomaticGearbox::GetClutchDisengagement()
            : ClutchSystem::UpdatePedal(clutch, rawThrottle, isEngineOn);
    PedalModel::Update(rawThrottle, rawBrake, simulatedClutch, manualGear,
                       forwardSpeed, automaticMode, isEngineOn);
    float throttle = PedalModel::GetThrottle();
    float brake = PedalModel::GetBrake();

    if (automaticMode) {
      if (!scooterVehicle) {
        AutomaticGearbox::UpdateSelector(
            vehicle, shiftUpPressed, shiftDownPressed, brake, forwardSpeed);
      }
      manualGear = AutomaticGearbox::Update(
          vehicle, data, maxGear, throttle, brake, forwardSpeed, isEngineOn);
      simulatedClutch = AutomaticGearbox::GetClutchDisengagement();
    } else {
      manualGear = GearLogic::Update(
          vehicle, data, maxGear, shiftUpPressed, shiftDownPressed,
          simulatedClutch, throttle,
          speedKmH, isEngineOn, grindWarningTimer);
    }

    const bool parkingBrakeOn = ParkingBrake::Update(
        vehicle, data, speedKmH, throttle, manualGear, isEngineOn);

    float tcsThrottle = throttle;
    float absBrake = brake;
    TractionControl::Update(vehicle, data, forwardSpeed, manualGear,
                            simulatedClutch, tcsThrottle);
    absBrake = BrakeSystem::UpdateABS(
        vehicle, data, absBrake, forwardSpeed, manualGear < 0);

    if (TractionControl::IsTCSActive())
      LOG_VERBOSE(Physics, "TCS active — throttle limited to %.3f", tcsThrottle);
    if (BrakeSystem::IsABSActive())
      LOG_VERBOSE(Physics, "ABS active — brake limited to %.3f", absBrake);

    const float turboMul =
        TurboSystem::Update(vehicle, data, rpm, throttle, isEngineOn);
    if (turboMul > 1.05f) {
      LOG_DEBUG_T(Turbo, 1000,
                  "Boost telemetry: native power, mul=%.3f press=%.3f",
                  turboMul,
                  TurboSystem::GetBoostPressure());
    }

    const float driveThrottle =
        automaticMode && AutomaticGearbox::IsSport()
            ? std::pow(std::clamp(tcsThrottle, 0.0f, 1.0f),
                       1.0f - std::clamp(Config::AutomaticSTorqueBoost,
                                         0.0f, 0.50f))
            : tcsThrottle;
    InputHandler::ApplyGameControls(vehicle, manualGear, simulatedClutch,
                                    driveThrottle, absBrake, maxGear,
                                    forwardSpeed);

    const float clutchEngagement =
        automaticMode ? AutomaticGearbox::GetCoupling()
                      : ClutchSystem::GetEngagement();

    if (automaticMode) {
      AutomaticGearbox::ApplyToMemory(vehicle, data, manualGear,
                                      driveThrottle);
    } else {
      GearLogic::ApplyToMemory(vehicle, data, manualGear, maxGear,
                               simulatedClutch, throttle, speedKmH);
      ClutchSystem::ApplyToVehicle(data, manualGear, forwardSpeed);
    }
    const bool engineStall = EngineModel::Update(
        vehicle, data, manualGear, maxGear, simulatedClutch,
        clutchEngagement, driveThrottle, absBrake, forwardSpeed, isEngineOn,
        automaticMode);
    const float sportTorque =
        automaticMode && AutomaticGearbox::IsSport()
            ? 1.0f + std::clamp(Config::AutomaticSTorqueBoost, 0.0f, 0.50f)
            : 1.0f;
    const float powerMultiplier =
        std::clamp(EngineModel::GetDriveTorqueFactor() * sportTorque,
                   0.0f, 1.80f);
    VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(vehicle, powerMultiplier);

    LaunchControl::Update(data, manualGear, simulatedClutch, throttle,
                          absBrake, forwardSpeed, isEngineOn, automaticMode);
    GearboxSystem::Update(vehicle, data, manualGear, maxGear, simulatedClutch,
                          throttle, isEngineOn);

    const bool shiftStall = GearboxSystem::ConsumeStallRequest();
    if ((engineStall || shiftStall) && isEngineOn) {
      isEngineOn = false;
      VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(vehicle, 0.0f);
      EngineModel::RestoreRuntimeDriveForce(data);
      VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
      LOG_WARN(Physics,
               "Drivetrain stall: gear=%d spd=%.1fkm/h load=%.3f heat=%.3f",
               manualGear, speedKmH, EngineModel::GetLoad(),
               ClutchSystem::GetHeat());
      Renderer::ShowNotification("Engine Stalled! (Drivetrain load)");
    }

    // Fuel
    const bool fuelStall =
        FuelSystem::Update(vehicle, data, throttle, rpm, isEngineOn, speedKmH);

    if (fuelStall && isEngineOn) {
      isEngineOn = false;
      EngineModel::RestoreRuntimeDriveForce(data);
      VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
      LOG_ERROR(Fuel, "Fuel stall! fuel=%.3f oilTemp=%.3f",
                FuelSystem::GetFuelLevel(), FuelSystem::GetOilTemperature());
      Renderer::ShowNotification("~r~OUT OF FUEL! Engine stopped.");
    }

    // Memory writes + lights
    LightsLogic::Update(vehicle, data, manualGear,
                        InputHandler::GetSmoothedBrake(), throttle);
    if (automaticMode) {
      AutomaticGearbox::ApplyToMemory(vehicle, data, manualGear,
                                      driveThrottle);
    } else {
      GearLogic::ApplyToMemory(vehicle, data, manualGear, maxGear,
                               simulatedClutch, throttle, speedKmH);
      ClutchSystem::ApplyToVehicle(data, manualGear, forwardSpeed);
    }

    static DWORD s_lastStatusLog = 0;
    if (GetTickCount() - s_lastStatusLog > 1000) {
      const uint8_t ratioIndex =
          manualGear < 0 ? 0 : static_cast<uint8_t>(manualGear);
      LOG_INFO(
          Gear,
          "STATUS: Mode=%d Profile=%s Selector=%s Selected=%d Mem=%u Next=%u "
          "PedalClutch=%.3f Key=%d Coupling=%.3f PowerMul=%.3f Limiter=%d "
          "MemClutch=%.3f Actuator=%.3f Throttle=%.3f MemThrottle=%.3f "
          "Brake=%.3f RPM=%.3f AutoRPM=%.3f AutoPhase=%s AutoTarget=%d "
          "AutoRecover=%d "
          "NativePatch=%d RPMOwned=%d ControlledRPM=%.3f RPMTarget=%.3f "
          "WheelRPM=%.3f CutFix=%d MemPedal=%.3f "
          "SpeedKmH=%.1f SignedMps=%.2f Ratio=%.4f MaxVel=%.2f EstFlat=%.2f "
          "Handling=%d Load=%.3f TorqueReserve=%.3f TorqueCurve=%.3f "
          "EngineRPM=%.0f IdleRPM=%.0f RedlineRPM=%.0f Condition=%.3f "
          "Lug=%.3f Stall=%.3f "
          "Accel=%.3f LowRec=%.3f RuntimeForce=%.4f ForceMul=%.3f "
          "ForceApplied=%.4f "
          "EngMod=%d/%d TransMod=%d/%d Race=%d Quick=%d PowerShift=%d "
          "Clash=%.3f Shock=%.3f ShiftPenalty=%.3f ShiftQuick=%d "
          "ShiftPower=%d ShiftSynchro=%d Money=%d "
          "Engine=%d Actual=%d Start=%d "
          "TCSEn=%d TCSReady=%d TCSWheels=%d TCSDriven=%d "
          "TCSSlip=%.3f TCSCut=%.3f TCS=%d "
          "ABSEn=%d ABSReady=%d ABSWheels=%d ABSSlip=%.3f ABSLevel=%.3f ABS=%d "
          "LCEn=%d LCArmed=%d LCCut=%d",
          transmissionMode, VehicleProfile::GetName(vehicleProfile),
          automaticMode ? AutomaticGearbox::GetSelectorName() : "M",
          manualGear, static_cast<unsigned>(data.GetGear()),
          static_cast<unsigned>(data.GetNextGear()), simulatedClutch,
          (!automaticMode && InputHandler::IsClutchDown()) ? 1 : 0,
          InputHandler::GetDriveCoupling(), powerMultiplier,
          EngineModel::GetState().redlineCut ? 1 : 0, data.GetClutch(),
          automaticMode ? AutomaticGearbox::GetCoupling()
                        : ClutchSystem::GetNativeActuator(),
          driveThrottle, data.GetThrottle(), absBrake, data.GetRPM(),
          automaticMode ? AutomaticGearbox::GetState().decisionRPM : -1.0f,
          automaticMode ? AutomaticGearbox::GetShiftPhaseName() : "-",
          automaticMode ? AutomaticGearbox::GetState().pendingGear : 0,
          automaticMode && AutomaticGearbox::GetState().rpmRecovery ? 1 : 0,
          GearboxPatches::IsApplied() ? 1 : 0,
          EngineModel::GetState().rpmOwned ? 1 : 0,
          EngineModel::GetState().controlledRPM,
          EngineModel::GetState().connectedRPMTarget,
          EngineModel::GetState().wheelRPM,
          EngineModel::GetState().nativeCutRecovered ? 1 : 0,
          data.GetThrottlePedal(),
          speedKmH, forwardSpeed, data.GetGearRatio(ratioIndex),
          data.GetDriveMaxFlatVel(),
          EngineModel::GetState().estimatedFlatVelocity,
          EngineModel::GetState().handlingBacked ? 1 : 0,
          EngineModel::GetLoad(), EngineModel::GetTorqueReserve(),
          EngineModel::GetState().torqueCurve,
          EngineModel::GetState().estimatedEngineRPM,
          EngineModel::GetState().estimatedIdlePhysicalRPM,
          EngineModel::GetState().estimatedRedlineRPM,
          EngineModel::GetState().engineCondition,
          EngineModel::GetState().lugSeverity,
          EngineModel::GetStallProgress(),
          EngineModel::GetState().longitudinalAcceleration,
          EngineModel::GetState().lowRpmRecovery,
          EngineModel::GetState().runtimeDriveForceBase,
          EngineModel::GetState().runtimeDriveForceMultiplier,
          EngineModel::GetState().runtimeDriveForceApplied,
          VehicleUpgrades::GetState().engineLevel,
          VehicleUpgrades::GetState().engineMaxLevel,
          VehicleUpgrades::GetState().transmissionLevel,
          VehicleUpgrades::GetState().transmissionMaxLevel,
          VehicleUpgrades::GetState().raceTransmission ? 1 : 0,
          VehicleUpgrades::GetState().quickshifter ? 1 : 0,
          VehicleUpgrades::GetState().powershifter ? 1 : 0,
          GearboxSystem::GetState().clashSeverity,
          GearboxSystem::GetState().shockRemaining,
          GearboxSystem::GetState().penaltyMultiplier,
          GearboxSystem::GetState().quickShift ? 1 : 0,
          GearboxSystem::GetState().powerShift ? 1 : 0,
          GearboxSystem::GetState().synchroShift ? 1 : 0,
          GearboxSystem::GetState().moneyShift ? 1 : 0,
          isEngineOn ? 1 : 0, actualEngineOn ? 1 : 0,
          engineStarting ? 1 : 0,
          TractionControl::GetState().enabled ? 1 : 0,
          TractionControl::GetState().wheelDataValid ? 1 : 0,
          TractionControl::GetState().validWheelCount,
          TractionControl::GetState().drivenWheelCount,
          TractionControl::GetState().slipRatio,
          TractionControl::GetState().cutLevel,
          TractionControl::IsTCSActive() ? 1 : 0,
          BrakeSystem::GetState().absEnabled ? 1 : 0,
          BrakeSystem::GetState().wheelDataValid ? 1 : 0,
          BrakeSystem::GetState().validWheelCount,
          BrakeSystem::GetState().wheelSlip,
          BrakeSystem::GetState().absLevel,
          BrakeSystem::IsABSActive() ? 1 : 0,
          LaunchControl::GetState().enabled ? 1 : 0,
          LaunchControl::GetState().armed ? 1 : 0,
          LaunchControl::GetState().limiting ? 1 : 0);
      s_lastStatusLog = GetTickCount();
    }

    // ── HUD ───────────────────────────────────────────────────────────────
    if (!Menu::IsOpen()) {
      Renderer::DrawGearHUD(
          manualGear, maxGear, activeSignal, isEngineOn, engineStarting,
          transmissionMode,
          automaticMode ? AutomaticGearbox::GetSelectorName() : nullptr);
    }

    if (grindWarningTimer > 0 && !Menu::IsOpen()) {
      --grindWarningTimer;
      Renderer::DrawGrindWarning();
      LOG_WARN_T(Gear, 2000, "Gear grind: gear=%d clutch=%.3f rpm=%.4f",
                 manualGear, simulatedClutch, rpm);
    }

    if (Config::DebugOverlay) {
      Renderer::DrawDebugOverlay(
          manualGear, static_cast<unsigned>(data.GetGear()),
          static_cast<unsigned>(data.GetNextGear()), rpm, data.GetClutch(),
          VehicleData::GetOffsetSourceName());
    }

    if (Config::OverlayBars && !Menu::IsOpen()) {
      Renderer::DrawPedalsOverlay(rpm,
                                  automaticMode ? -1.0f : simulatedClutch,
                                  throttle,
                                  InputHandler::GetSmoothedBrake());
      Renderer::DrawSimulationOverlay(
          FuelSystem::GetFuelLevel(), FuelSystem::GetOilTemperature(),
          GearboxSystem::GetHealth(), ClutchSystem::GetHeat(),
          ParkingBrake::IsEngaged(), BrakeSystem::IsABSActive(),
          EngineModel::GetEngineBrake());

      std::string warnings;
      if (TractionControl::IsTCSActive()) warnings += "~y~TCS ";
      if (BrakeSystem::IsABSActive()) warnings += "~y~ABS ";
      if (LaunchControl::IsActive()) warnings += "~b~LC ";
      if (PedalModel::IsBrakeOverrideActive()) warnings += "~r~BTO ";
      if (ClutchSystem::IsDumpActive()) warnings += "~r~CLUTCH DUMP ";
      if (GearboxSystem::GetState().moneyShift) warnings += "~r~OVERREV ";
      if (BrakeSystem::GetFadeLevel() > 0.01f) warnings += "~r~BRAKE FADE ";
      if (GearboxSystem::GetState().clashActive) warnings += "~r~GEAR CLASH ";
      if (TurboSystem::HasTurbo() &&
          TurboSystem::GetBoostPressure() > 0.05f)
        warnings += "~b~BOOST ";
      if (!warnings.empty()) {
        Renderer::DrawTextOverlay(
            warnings.c_str(), 0.88f, 0.80f, 0.34f,
            255, 255, 255, 255, 0, true, true);
      }
    }
    Menu::Draw();
  } // while (true)
}
