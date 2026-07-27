#pragma once

class VehicleData;
using Vehicle = int;

namespace EngineModel {

struct State {
  float idleRPM = 0.2f;
  float inertia = 1.0f;
  float load = 0.0f;
  float stallProgress = 0.0f;
  float engineBrake = 0.0f;
  float expectedRPM = 0.2f;
  float creepThrottle = 0.0f;
  float torqueReserve = 0.0f;
  float torqueCurve = 1.0f;
  float previousRPM = 0.2f;
  float previousThrottle = 0.0f;
  float controlledRPM = 0.2f;
  float wheelRPM = 0.2f;
  float connectedRPMTarget = 0.2f;
  float estimatedFlatVelocity = 0.0f;
  float gearLimitSpeedMps = 0.0f;
  float drivenWheelSpeedMps = 0.0f;
  float rollingRadius = 0.34f;
  float driveTorqueFactor = 1.0f;
  float longitudinalAcceleration = 0.0f;
  float lowRpmRecovery = 0.0f;
  float estimatedEngineRPM = 800.0f;
  float estimatedIdlePhysicalRPM = 800.0f;
  float estimatedRedlineRPM = 6800.0f;
  float initialDriveMaxFlatVel = 0.0f;
  float engineCondition = 1.0f;
  float lugSeverity = 0.0f;
  float waterIngestion = 0.0f;
  float oilStarvation = 0.0f;
  float revHangRemaining = 0.0f;
  float hardBrakeStallProgress = 0.0f;
  float previousDirectionalSpeed = 0.0f;
  bool freeRevActive = false;
  bool rpmOwned = false;
  bool nativeCutRecovered = false;
  bool redlineCut = false;
  bool redlineHandlingBacked = false;
  bool handlingBacked = false;
  bool adaptiveGearing = false;
  bool speedSampleValid = false;
  bool wheelTelemetryValid = false;
  bool burnoutActive = false;
  bool airborne = false;
  bool upsideDown = false;
  bool environmentStall = false;
  bool hillRollback = false;
  bool hardBrakeStall = false;
};

void Reset();

// Hitung torsi idle yang perlu dikirim ke control axis sebelum native GTA
// memproses frame. Nilai pedal pemain tetap terpisah dari creep ini.
float PrepareIdleDrive(Vehicle vehicle, VehicleData &data, int gear,
                       int maxGear, float clutchEngagement, float throttle,
                       float brake, float speedMps, bool engineOn,
                       bool automaticMode, bool gentleClutchRelease);

// Return true kalau beban drivetrain sukses bikin mesin mati.
bool Update(Vehicle vehicle, VehicleData &data, int gear, int maxGear,
            float clutchDisengagement, float clutchEngagement,
            float throttle, float brake, float speedMps, bool engineOn,
            bool automaticMode = false);

float GetLoad();
float GetStallProgress();
float GetEngineBrake();
float GetInertia();
float GetExpectedRPM();
float GetCreepThrottle();
float GetTorqueReserve();
float GetDriveTorqueFactor();
const State &GetState();

} // namespace EngineModel
