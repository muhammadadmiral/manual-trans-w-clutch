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
#include "../Vehicle/Brakes/BrakeSystem.h"
#include "../Vehicle/Brakes/ParkingBrake.h"
#include "../Vehicle/Clutch/ClutchSystem.h"
#include "../Vehicle/Engine/EngineModel.h"
#include "../Vehicle/Engine/FuelSystem.h"
#include "../Vehicle/Engine/LaunchControl.h"
#include "../Vehicle/Engine/TractionControl.h"
#include "../Vehicle/Engine/TurboSystem.h"
#include "../Vehicle/Gears/GearboxSystem.h"
#include "../Vehicle/Gears/GearLogic.h"
#include "../Vehicle/LightsLogic.h"
#include "../Vehicle/TelemetryLogger.h"
#include "../Vehicle/VehicleData.h"

#include <Windows.h>
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
  Vehicle activeVehicle = 0;
  bool activeLayoutValid = false;
  bool isEngineOn = true;
  int grindWarningTimer = 0;
  int manualGear = 0;
  int activeSignal = 0;
  ULONGLONG vehicleEnterTick = 0;

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
    }

    Menu::Update();
    const Ped playerPed = PLAYER::PLAYER_PED_ID();

    // ── Not in a vehicle ──────────────────────────────────────────────────
    if (!PED::IS_PED_IN_ANY_VEHICLE(playerPed, FALSE)) {
      if (activeVehicle) {
        LOG_INFO(Script, "Player exited vehicle %d — resetting session state",
                 activeVehicle);
      }
      activeVehicle = 0;
      activeLayoutValid = false;
      activeSignal = 0;
      InputHandler::ResetEdges();
      Menu::Draw();
      continue;
    }

    const Vehicle vehicle = PED::GET_VEHICLE_PED_IS_IN(playerPed, FALSE);
    if (!IsValidVehicle(vehicle) || !IsPlayerDriving(playerPed, vehicle)) {
      activeVehicle = 0;
      activeLayoutValid = false;
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
      sprintf_s(notify, "Manual trans: %s | build %s | G:%X N:%X RPM:%X CLT:%X",
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

    // ── Vehicle change ────────────────────────────────────────────────────
    if (vehicle != activeVehicle) {
      LOG_INFO(Script, "Entered vehicle handle=%d maxGear=%d", vehicle,
               maxGear);
      activeVehicle = vehicle;
      activeLayoutValid = true;

      isEngineOn = Config::RequireColdStart ? false : actualEngineOn;
      if (Config::RequireColdStart) {
        VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
        LOG_INFO(Script,
                 "Cold start required — engine forced OFF for vehicle %d",
                 vehicle);
      }

      GearLogic::Reset(0);
      GearboxSystem::Reset();
      ClutchSystem::Reset();
      EngineModel::Reset();
      LaunchControl::Reset();
      BrakeSystem::Reset();
      ParkingBrake::Reset();
      FuelSystem::Reset();
      TractionControl::Reset();
      TurboSystem::Reset();
      TurboSystem::InitializeForVehicle(vehicle);

      if (TelemetryLogger::IsLogging()) {
        TelemetryLogger::StopSession();
        LOG_INFO(Script, "Telemetry session stopped to prevent disk bloat.");
      }

      vehicleEnterTick = GetTickCount64();
      activeSignal = 0;
    }

    // Skip a short grace period while vehicle physics initializes
    if (GetTickCount64() - vehicleEnterTick < 500) {
      Menu::Draw();
      continue;
    }

    InputHandler::Update();

    // ── Engine toggle key ─────────────────────────────────────────────────
    if (InputHandler::IsEngineJustPressed()) {
      isEngineOn = !isEngineOn;
      VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, isEngineOn ? TRUE : FALSE, TRUE,
                                     TRUE);
      LOG_INFO(Script, "Engine key → %s", isEngineOn ? "ON" : "OFF");
    } else if (!isEngineOn && actualEngineOn) {
      // Game AI turned it back on — force our state.
      VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
      LOG_DEBUG(Script,
                "Engine mismatch corrected: we=OFF game=ON → forcing OFF");
    } else if (isEngineOn && !actualEngineOn) {
      // Died externally (fire / destroyed / fuel empty)
      isEngineOn = false;
      LOG_WARN(Script,
               "Engine died externally (fire/destroyed). Reflecting state.");
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
      LOG_DEBUG_T(Script, 3000, "Deluxo hover active — skipping");
      Menu::Draw();
      continue;
    }

    // ── Per-frame values ──────────────────────────────────────────────────
    const float vehicleSpeed = ENTITY::GET_ENTITY_SPEED(vehicle);
    const float speedKmH = vehicleSpeed * 3.6f;
    const float forwardSpeed = ENTITY::GET_ENTITY_SPEED_VECTOR(vehicle, TRUE).y;
    // A digital clutch must disconnect on the first pressed frame. Attack
    // still shapes release/travel, but can never leave residual drive while
    // the key is physically held.
    const float clutch = InputHandler::IsClutchDown()
        ? 1.0f
        : InputHandler::GetSmoothedClutch();
    const float throttle = InputHandler::GetSmoothedThrottle();
    const float rpm = data.GetRPM();

    // (Spammy input logging removed)
    // ── Subsystem updates ─────────────────────────────────────────────────
    float simulatedClutch =
        ClutchSystem::UpdatePedal(clutch, throttle, isEngineOn);
    const bool parkingBrakeOn = ParkingBrake::Update(
        vehicle, data, speedKmH, throttle, manualGear, isEngineOn);

    float tcsThrottle = throttle;
    float absBrake = InputHandler::GetSmoothedBrake();
    TractionControl::Update(vehicle, data, forwardSpeed, manualGear,
                            simulatedClutch, tcsThrottle);
    if (manualGear >= 0)
      absBrake = BrakeSystem::UpdateABS(vehicle, data, absBrake, forwardSpeed);

    if (TractionControl::IsTCSActive())
      LOG_VERBOSE(Physics, "TCS active — throttle limited to %.3f", tcsThrottle);
    if (BrakeSystem::IsABSActive())
      LOG_VERBOSE(Physics, "ABS active — brake limited to %.3f", absBrake);

    static DWORD s_lastStatusLog = 0;
    if (GetTickCount() - s_lastStatusLog > 1000) {
      LOG_INFO(Gear, "STATUS: Selected=%d MemGear=%u Next=%u "
               "PedalClutch=%.3f ClutchKey=%d Coupling=%.3f "
               "NativeClutch=%.3f Actuator=%.3f Throttle=%.3f Brake=%.3f "
               "RPM=%.3f SpeedKmH=%.1f SignedMps=%.2f "
               "Inertia=%.3f ExpectedRPM=%.3f Load=%.3f Creep=%.3f "
               "TorqueReserve=%.3f Stall=%.3f Wheels=%d "
               "Clash=%.3f Shock=%.3f | TCS=%.2f ABS=%.2f | Sig=%d Rev=%d",
               manualGear, static_cast<unsigned>(data.GetGear()),
               static_cast<unsigned>(data.GetNextGear()), simulatedClutch,
               InputHandler::IsClutchDown() ? 1 : 0,
               InputHandler::GetDriveCoupling(), data.GetClutch(),
               ClutchSystem::GetNativeActuator(), tcsThrottle, absBrake, rpm,
               speedKmH, forwardSpeed, EngineModel::GetInertia(),
               EngineModel::GetExpectedRPM(), EngineModel::GetLoad(),
               EngineModel::GetCreepThrottle(),
               EngineModel::GetTorqueReserve(),
               EngineModel::GetStallProgress(),
               static_cast<int>(data.GetWheelCount()),
               GearboxSystem::GetState().clashSeverity,
               GearboxSystem::GetState().shockRemaining,
               TractionControl::IsTCSActive() ? 1.0f : 0.0f, BrakeSystem::IsABSActive() ? 1.0f : 0.0f,
               activeSignal, (manualGear == -1) ? 1 : 0);
      s_lastStatusLog = GetTickCount();
    }

    float turboMul =
        TurboSystem::Update(vehicle, data, rpm, throttle, isEngineOn);
    if (TurboSystem::HasTurbo())
      VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(vehicle, turboMul);
    if (turboMul > 1.05f) {
      LOG_DEBUG_T(Turbo, 1000, "Boost active: mul=%.3f press=%.3f", turboMul,
                  TurboSystem::GetBoostPressure());
    }

    InputHandler::ApplyGameControls(manualGear, simulatedClutch, tcsThrottle,
                                    maxGear, forwardSpeed);

    // Gear logic
    manualGear = GearLogic::Update(
        vehicle, data, maxGear, InputHandler::IsShiftUpJustPressed(),
        InputHandler::IsShiftDownJustPressed(), simulatedClutch, throttle,
        speedKmH, isEngineOn, grindWarningTimer);

    GearLogic::ApplyToMemory(vehicle, data, manualGear, maxGear,
                             simulatedClutch, throttle, speedKmH);
    ClutchSystem::ApplyToVehicle(data, manualGear, forwardSpeed);
    const bool engineStall = EngineModel::Update(
        vehicle, data, manualGear, maxGear, simulatedClutch,
        ClutchSystem::GetEngagement(), throttle, forwardSpeed, isEngineOn);
    LaunchControl::Update(data, manualGear, simulatedClutch, throttle,
                          forwardSpeed, isEngineOn);
    GearboxSystem::Update(data, manualGear, maxGear, simulatedClutch,
                          throttle, isEngineOn);

    if (engineStall && isEngineOn) {
      isEngineOn = false;
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
      VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
      LOG_ERROR(Fuel, "Fuel stall! fuel=%.3f oilTemp=%.3f",
                FuelSystem::GetFuelLevel(), FuelSystem::GetOilTemperature());
      Renderer::ShowNotification("~r~OUT OF FUEL! Engine stopped.");
    }

    // Memory writes + lights
    LightsLogic::Update(vehicle, data, manualGear,
                        InputHandler::GetSmoothedBrake(), throttle);

    // ── HUD ───────────────────────────────────────────────────────────────
    Renderer::DrawGearHUD(manualGear, maxGear, activeSignal, isEngineOn);

    if (grindWarningTimer > 0) {
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

    if (Config::OverlayBars) {
      Renderer::DrawPedalsOverlay(rpm, simulatedClutch, throttle,
                                  InputHandler::GetSmoothedBrake());
      Renderer::DrawSimulationOverlay(
          FuelSystem::GetFuelLevel(), FuelSystem::GetOilTemperature(),
          GearboxSystem::GetHealth(), ClutchSystem::GetHeat(),
          ParkingBrake::IsEngaged(), BrakeSystem::IsABSActive(),
          EngineModel::GetEngineBrake());

      Renderer::DrawTextOverlay(
          (std::string("TCS: ") +
           (TractionControl::IsTCSActive() ? "~y~ON" : "~g~OK") +
           " | ABS: " + (BrakeSystem::IsABSActive() ? "~y~ON" : "~g~OK") +
           (LaunchControl::IsActive() ? " | LC: ~b~HOLD" : "") +
           (GearboxSystem::GetState().clashActive ? " | GEAR: ~r~CLASH" : "") +
           (TurboSystem::HasTurbo()
                ? (" | BOOST: " +
                   std::to_string(TurboSystem::GetBoostPressure()).substr(0, 4))
                : ""))
              .c_str(),
          Config::OverlayPosX,
          Config::OverlayPosY + Config::OverlayBarHeight * 5.0f, 0.35f);
    }
    Menu::Draw();
  } // while (true)
}
