#define NOMINMAX
#include "sdk/inc/main.h"
#include "sdk/inc/natives.h"
#include "src/Core/Config.h"
#include "src/Core/InputHandler.h"
#include "src/Core/Menu.h"
#include "src/Core/Renderer.h"
#include "src/Vehicle/GearLogic.h"
#include "src/Vehicle/VehicleData.h"

#include <Windows.h>
#include <string>
#include <unordered_map>

HMODULE g_pluginModule = nullptr;

namespace {

bool IsVehicleClassExcluded(int vehicleClass) {
  for (const int excluded : Config::ExcludedVehicleClasses) {
    if (excluded == vehicleClass)
      return true;
  }
  return false;
}

bool ComputeIsValidVehicle(Vehicle vehicle) {
  if (vehicle == 0 || !ENTITY::DOES_ENTITY_EXIST(vehicle))
    return false;

  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);

  if (VEHICLE::IS_THIS_MODEL_A_PLANE(model) ||
      VEHICLE::IS_THIS_MODEL_A_HELI(model) ||
      VEHICLE::IS_THIS_MODEL_A_BOAT(model) ||
      VEHICLE::IS_THIS_MODEL_A_JETSKI(model) ||
      VEHICLE::IS_THIS_MODEL_A_TRAIN(model) ||
      VEHICLE::IS_THIS_MODEL_A_BICYCLE(model)) {
    return false;
  }

  if (!Config::AllowQuadbikes && VEHICLE::IS_THIS_MODEL_A_QUADBIKE(model)) {
    return false;
  }

  if (IsVehicleClassExcluded(VEHICLE::GET_VEHICLE_CLASS(vehicle))) {
    return false;
  }

  return true;
}

bool IsValidVehicle(Vehicle vehicle) {
  if (vehicle == 0 || !ENTITY::DOES_ENTITY_EXIST(vehicle))
    return false;

  static std::unordered_map<Hash, bool> classificationCache;
  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
  const auto cached = classificationCache.find(model);
  if (cached != classificationCache.end())
    return cached->second;

  const bool valid = ComputeIsValidVehicle(vehicle);
  if (valid) {
    classificationCache.emplace(model, true);
  }
  return valid;
}

bool IsPlayerDriving(Ped playerPed, Vehicle vehicle) {
  return VEHICLE::GET_PED_IN_VEHICLE_SEAT(vehicle, -1, 0) == playerPed;
}

} // namespace

void ScriptMain() {
  while (true) {
    scriptWait(1000);
    const Ped playerPed = PLAYER::PLAYER_PED_ID();
    if (ENTITY::DOES_ENTITY_EXIST(playerPed) &&
        !PED::IS_PED_INJURED(playerPed)) {
      break;
    }
  }

  if (!VehicleData::Initialize(g_pluginModule)) {
    const std::string buildVersion = VehicleData::GetGameBuildVersion();
    char failMessage[256]{};
    sprintf_s(failMessage, "Manual transmission disabled (build %s): %s",
              buildVersion.empty() ? "unknown" : buildVersion.c_str(),
              VehicleData::GetLastFailureReason());
    Renderer::ShowNotification(failMessage);
    return;
  }

  Config::ReadConfig(g_pluginModule);

  bool notificationShown = false;
  Vehicle activeVehicle = 0;
  bool activeLayoutValidated = false;
  bool isEngineOn = true;
  int grindWarningTimer = 0;
  int manualGear = 0; // Updated by GearLogic
  static ULONGLONG s_vehicleEnterTime = 0;

  // Turn signal state: 0 = off, 1 = left, 2 = right.
  int activeSignal = 0;

  while (true) {
    scriptWait(0);
    Menu::Update();

    const Ped playerPed = PLAYER::PLAYER_PED_ID();
    if (!PED::IS_PED_IN_ANY_VEHICLE(playerPed, FALSE)) {
      activeVehicle = 0;
      activeLayoutValidated = false;
      activeSignal = 0;
      InputHandler::ResetEdges();
      Menu::Draw();
      continue;
    }

    const Vehicle vehicle = PED::GET_VEHICLE_PED_IS_IN(playerPed, FALSE);
    if (!IsValidVehicle(vehicle) || !IsPlayerDriving(playerPed, vehicle)) {
      activeVehicle = 0;
      activeLayoutValidated = false;
      activeSignal = 0;
      InputHandler::ResetEdges();
      Menu::Draw();
      continue;
    }

    if (!notificationShown && VehicleData::IsInitialized()) {
      notificationShown = true;
      const VehicleOffsets &offsets = VehicleData::GetResolvedOffsets();
      const std::string buildVersion = VehicleData::GetGameBuildVersion();
      char loadedMessage[256]{};
      sprintf_s(loadedMessage,
                "Manual transmission: %s | build %s | G:%X N:%X R:%X C:%X",
                VehicleData::GetOffsetSourceName(),
                buildVersion.empty() ? "unknown" : buildVersion.c_str(),
                offsets.Gear, offsets.NextGear, offsets.RPM, offsets.Clutch);
      Renderer::ShowNotification(loadedMessage);
    }

    const int maxGear = VEHICLE::_GET_VEHICLE_MAX_DRIVE_GEAR_COUNT(vehicle);
    VehicleData data(vehicle);
    // This is just a read of the game's own state, used below to detect
    // *external* changes (stalls, the game killing the engine, etc). It must
    // NOT be blindly copied into isEngineOn every frame - see comment below.
    const bool actualEngineOn =
        VEHICLE::GET_IS_VEHICLE_ENGINE_RUNNING(vehicle) != 0;

    if (vehicle != activeVehicle) {
      activeVehicle = vehicle;
      // The memory layout is identical for all vehicles in the same game
      // session. DO NOT recalibrate on vehicle change! It causes crashes on
      // uninitialized physics objects.
      activeLayoutValidated = true;

      if (Config::RequireColdStart) {
        VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
        isEngineOn = false;
      } else {
        isEngineOn = actualEngineOn;
      }

      GearLogic::Reset(0); // Neutral
      s_vehicleEnterTime = GetTickCount64();
      activeSignal = 0;
    }

    if (GetTickCount64() - s_vehicleEnterTime < 500) {
      // Vehicle is still initializing. Skip processing this frame to prevent
      // crashes.
      Menu::Draw();
      continue;
    }

    InputHandler::Update();

    if (InputHandler::IsEngineJustPressed()) {
      isEngineOn = !isEngineOn;
      // instantly = TRUE (3rd param) in BOTH directions. This is the fix for
      // "engine won't turn on at all": with instantly=FALSE, the engine
      // plays its multi-frame start-up animation, during which
      // GET_IS_VEHICLE_ENGINE_RUNNING still reports FALSE. The old code then
      // saw "not running yet" on the very next frame and force-killed the
      // engine again below, every single frame - so it could never finish
      // starting, and calibration (which waits for isEngineOn to become
      // true) got stuck forever showing "Scanning...".
      VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, isEngineOn ? TRUE : FALSE, TRUE,
                                     TRUE);
    } else if (!isEngineOn && actualEngineOn) {
      // We want the engine off (cold start hold, or after a stall) but the
      // game still reports it running - correct it. Only fires on an actual
      // mismatch, not unconditionally every frame.
      VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, FALSE, TRUE, TRUE);
    } else if (isEngineOn && !actualEngineOn) {
      // Engine died from something other than us (fire, destroyed, ran out
      // of fuel, etc). Reflect that instead of pretending it's still on.
      isEngineOn = false;
    }

    // --- Turn signals (sein) ---
    // NOTE ON THE NATIVE PARAMETER: SET_VEHICLE_INDICATOR_LIGHTS's "indicator"
    // index has been documented inconsistently across SDK versions/dumps.
    // kIndicatorLeft/kIndicatorRight below are the only two lines you should
    // need to touch if your signals come out swapped in-game - just flip the
    // two values and rebuild.
    {
      constexpr int kIndicatorLeft = 1;
      constexpr int kIndicatorRight = 0;

      if (InputHandler::IsSignalLeftJustPressed()) {
        activeSignal = (activeSignal == 1) ? 0 : 1;
        VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(vehicle, kIndicatorRight, FALSE);
        VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(vehicle, kIndicatorLeft,
                                              activeSignal == 1);
      } else if (InputHandler::IsSignalRightJustPressed()) {
        activeSignal = (activeSignal == 2) ? 0 : 2;
        VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(vehicle, kIndicatorLeft, FALSE);
        VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(vehicle, kIndicatorRight,
                                              activeSignal == 2);
      }
    }

    if (!VehicleData::IsInitialized()) {
      const bool isRevving = InputHandler::GetSmoothedThrottle() > 0.5f;
      VehicleData::UpdateCalibration(g_pluginModule, vehicle, isEngineOn,
                                     isRevving);

      if (VehicleData::IsInitialized()) {
        activeLayoutValidated =
            data.HasPlausibleLayout(maxGear > 0 ? maxGear : 6);
        Renderer::ShowNotification(
            "Calibration complete! Manual transmission active.");
      } else {
        CalibrationState state = VehicleData::GetCalibrationState();
        std::string calibMsg = "Calibration: ";
        switch (state) {
        case CalibrationState::WaitingForEngineOff:
          calibMsg += "Turn off engine (press " +
                      std::string(1, (char)Config::KeyEngine) + ")";
          break;
        case CalibrationState::WaitingForEngineOn:
          calibMsg += "Turn ON engine (press " +
                      std::string(1, (char)Config::KeyEngine) + ") and idle";
          break;
        case CalibrationState::WaitingForRev:
          calibMsg += "Rev the engine (Hold W)";
          break;
        case CalibrationState::Done:
          calibMsg += "Success! Offsets saved.";
          break;
        case CalibrationState::Failed:
          calibMsg += "Failed. Try again.";
          break;
        default:
          calibMsg += "Scanning...";
          break;
        }
        Renderer::DrawTextOverlay(calibMsg.c_str(), 0.5f, 0.1f, 0.6f);
        Menu::Draw();
        continue;
      }
    }

    if (!activeLayoutValidated || !data.IsValid()) {
      Menu::Draw();
      continue;
    }

    // Continuous sanity re-check, not just once right after calibration.
    // Calibration-derived offsets (as opposed to the verified AOB pattern)
    // are a best-effort guess and can land on the wrong field even if the
    // values looked plausible for one instant. If the numbers drift outside
    // plausible range later, stop touching memory instead of continuing to
    // write into whatever that offset actually is.
    if (!data.HasPlausibleLayout(maxGear > 0 ? maxGear : 6)) {
      activeLayoutValidated = false;
      Renderer::ShowNotification(
          "Manual transmission: memory layout looks wrong, disabling for "
          "this vehicle. Try recalibrating.");
      Menu::Draw();
      continue;
    }

    const float vehicleSpeed = ENTITY::GET_ENTITY_SPEED(vehicle);
    const float speedKmH = vehicleSpeed * 3.6f;
    const float forwardSpeed = ENTITY::GET_ENTITY_SPEED_VECTOR(vehicle, TRUE).y;
    const float clutch = InputHandler::GetSmoothedClutch();
    const float throttle = InputHandler::GetSmoothedThrottle();

    InputHandler::ApplyGameControls(manualGear, clutch, data.GetRPM(), maxGear,
                                    forwardSpeed);

    const bool wasEngineOn = isEngineOn;

    manualGear = GearLogic::Update(
        vehicle, data, maxGear, InputHandler::IsShiftUpJustPressed(),
        InputHandler::IsShiftDownJustPressed(), clutch, throttle, speedKmH,
        isEngineOn, grindWarningTimer);

    if (wasEngineOn && !isEngineOn) {
      Renderer::ShowNotification(
          "Engine Stalled! (Depress clutch or shift to Neutral N)");
    }

    GearLogic::ApplyToMemory(vehicle, data, manualGear, clutch);

    Renderer::DrawGearHUD(manualGear, maxGear);

    if (grindWarningTimer > 0) {
      --grindWarningTimer;
      Renderer::DrawGrindWarning();
    }

    if (Config::DebugOverlay) {
      Renderer::DrawDebugOverlay(
          manualGear, static_cast<unsigned>(data.GetGear()),
          static_cast<unsigned>(data.GetNextGear()), data.GetRPM(),
          data.GetClutch(), VehicleData::GetOffsetSourceName());
    }

    if (Config::OverlayBars) {
      Renderer::DrawPedalsOverlay(data.GetRPM(), clutch, throttle,
                                  InputHandler::GetSmoothedBrake());
    }

    Menu::Draw();
  }
}

BOOL APIENTRY DllMain(HMODULE instance, DWORD reason, LPVOID) {
  switch (reason) {
  case DLL_PROCESS_ATTACH:
    g_pluginModule = instance;
    DisableThreadLibraryCalls(instance);
    scriptRegister(instance, ScriptMain);
    break;
  case DLL_PROCESS_DETACH:
    scriptUnregister(instance);
    break;
  default:
    break;
  }

  return TRUE;
}