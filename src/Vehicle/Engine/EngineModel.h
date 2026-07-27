#pragma once

class VehicleData;
using Vehicle = int;

namespace EngineModel {

struct State {
  float idleRPM = 0.0f;
  float minimumRunningRPM = 0.0f;
  float idleDeviation = 0.0f;
  float idleCalibrationTime = 0.0f;
  float inertia = 0.0f;
  float load = 0.0f;
  float stallProgress = 0.0f;
  float engineBrake = 0.0f;
  float expectedRPM = 0.0f;
  float creepThrottle = 0.0f;
  float torqueReserve = 0.0f;
  float torqueCurve = 1.0f;
  float previousRPM = 0.0f;
  float previousThrottle = 0.0f;
  float controlledRPM = 0.0f;
  float wheelRPM = 0.0f;
  float connectedRPMTarget = 0.0f;
  float estimatedFlatVelocity = 0.0f;
  float gearLimitSpeedMps = 0.0f;
  float drivenWheelSpeedMps = 0.0f;
  float rollingRadius = 0.0f;
  float driveTorqueFactor = 1.0f;
  float longitudinalAcceleration = 0.0f;
  float lowRpmRecovery = 0.0f;
  float estimatedEngineRPM = 0.0f;
  float estimatedIdlePhysicalRPM = 0.0f;
  float estimatedRedlineRPM = 0.0f;
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
  bool idleCalibrated = false;
};

void Reset();

// Feed-forward creep memakai idle rev ratio, rasio gigi dan flat velocity
// kendaraan yang diamati dari CVehicle/handling. Tidak menulis RPM.
float PrepareIdleDrive(Vehicle vehicle, VehicleData &data, int gear,
                       int maxGear, float clutchEngagement, float throttle,
                       float brake, float speedMps, bool engineOn,
                       bool automaticMode, bool gentleClutchRelease);

// Mengamati drivetrain native dan mengembalikan true jika putaran poros yang
// terhubung jatuh di bawah minimum idle yang dipelajari untuk kendaraan ini.
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
