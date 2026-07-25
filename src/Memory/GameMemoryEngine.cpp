#include "GameMemoryEngine.h"
#include "../../sdk/inc/main.h" // For getScriptHandleBaseAddress
#include "AOBScanner.h"
#include "OffsetResolver.h"

namespace GameMemory {



// --- CHandlingData Implementation ---
float CHandlingData::GetDriveForce() const {
  if (!IsValid() || m_offsets->DriveForce == 0)
    return 0.0f;
  __try {
    return *reinterpret_cast<float *>(m_address + m_offsets->DriveForce);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0.0f;
  }
}

float CHandlingData::GetDriveInertia() const {
  if (!IsValid() || m_offsets->DriveInertia == 0)
    return 0.0f;
  __try {
    return *reinterpret_cast<float *>(m_address + m_offsets->DriveInertia);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0.0f;
  }
}

float CHandlingData::GetDriveMaxFlatVel() const {
  if (!IsValid() || m_offsets->DriveMaxFlatVel == 0)
    return 0.0f;
  __try {
    return *reinterpret_cast<float *>(m_address + m_offsets->DriveMaxFlatVel);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0.0f;
  }
}

// --- CVehicle Implementation ---
CHandlingData CVehicle::GetHandlingData() const {
  if (!IsValid() || m_offsets->HandlingPtr == 0)
    return CHandlingData(0, m_offsets);
  __try {
    const uintptr_t handling =
        *reinterpret_cast<uintptr_t *>(m_address + m_offsets->HandlingPtr);
    if (!AOBScanner::IsReadable(handling, 0x100))
      return CHandlingData(0, m_offsets);
    return CHandlingData(handling, m_offsets);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return CHandlingData(0, m_offsets);
  }
}

uint8_t CVehicle::GetGear() const {
  if (!IsValid())
    return 0;
  __try {
    return *reinterpret_cast<uint8_t *>(m_address + m_offsets->Gear);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
}

uint8_t CVehicle::GetNextGear() const {
  if (!IsValid())
    return 0;
  __try {
    return *reinterpret_cast<uint8_t *>(m_address + m_offsets->NextGear);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
}

uint8_t CVehicle::GetTopGear() const {
  if (!IsValid())
    return 0;
  __try {
    return *reinterpret_cast<uint8_t *>(m_address + m_offsets->TopGear);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
}

float CVehicle::GetGearRatio(uint8_t gearIndex) const {
  if (!IsValid() || m_offsets->GearRatios == 0 || gearIndex > 32)
    return 0.0f;
  __try {
    const uintptr_t ratios =
        *reinterpret_cast<uintptr_t *>(m_address + m_offsets->GearRatios);
    if (!AOBScanner::IsReadable(ratios + gearIndex * sizeof(float),
                                sizeof(float)))
      return 0.0f;
    return *reinterpret_cast<float *>(ratios + gearIndex * sizeof(float));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0.0f;
  }
}

float CVehicle::GetClutch() const {
  if (!IsValid())
    return 0.0f;
  __try {
    return *reinterpret_cast<float *>(m_address + m_offsets->Clutch);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0.0f;
  }
}

float CVehicle::GetThrottle() const {
  if (!IsValid() || m_offsets->Throttle == 0)
    return 0.0f;
  __try {
    return *reinterpret_cast<float *>(m_address + m_offsets->Throttle);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0.0f;
  }
}

float CVehicle::GetThrottlePedal() const {
  if (!IsValid() || m_offsets->ThrottlePedal == 0)
    return 0.0f;
  __try {
    return *reinterpret_cast<float *>(m_address + m_offsets->ThrottlePedal);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0.0f;
  }
}

uint8_t CVehicle::GetWheelCount() const {
  if (!IsValid() || m_offsets->WheelCount == 0)
    return 0;
  __try {
    const int count =
        *reinterpret_cast<int *>(m_address + m_offsets->WheelCount);
    return count > 0 && count <= 16 ? static_cast<uint8_t>(count) : 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
}

WheelTelemetry CVehicle::GetWheelTelemetry(uint8_t index) const {
  WheelTelemetry out{};
  const uint8_t count = GetWheelCount();
  if (!IsValid() || index >= count || m_offsets->WheelsPtr == 0 ||
      m_offsets->WheelAngularVelocity == 0)
    return out;

  __try {
    const uintptr_t wheelArray =
        *reinterpret_cast<uintptr_t *>(m_address + m_offsets->WheelsPtr);
    if (!AOBScanner::IsReadable(wheelArray + index * sizeof(uintptr_t),
                                sizeof(uintptr_t)))
      return out;
    const uintptr_t wheel =
        *reinterpret_cast<uintptr_t *>(wheelArray +
                                      index * sizeof(uintptr_t));
    if (!AOBScanner::IsReadable(wheel + m_offsets->WheelAngularVelocity,
                                sizeof(float)))
      return out;

    out.angularVelocity =
        *reinterpret_cast<float *>(wheel + m_offsets->WheelAngularVelocity);
    if (m_offsets->WheelLoad != 0)
      out.load = *reinterpret_cast<float *>(wheel + m_offsets->WheelLoad);
    if (m_offsets->WheelBrakePressure != 0)
      out.brakePressure =
          *reinterpret_cast<float *>(wheel + m_offsets->WheelBrakePressure);
    if (m_offsets->WheelPower != 0)
      out.power = *reinterpret_cast<float *>(wheel + m_offsets->WheelPower);
    out.valid = true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return WheelTelemetry{};
  }
  return out;
}

float CVehicle::GetRPM() const {
  if (!IsValid())
    return 0.0f;
  __try {
    return *reinterpret_cast<float *>(m_address + m_offsets->RPM);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0.0f;
  }
}

float CVehicle::GetFuelLevel() const {
  if (!IsValid() || m_offsets->FuelLevel == 0)
    return 0.0f;
  __try {
    return *reinterpret_cast<float *>(m_address + m_offsets->FuelLevel);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0.0f;
  }
}

float CVehicle::GetHoverTransformRatioLerp() const {
  if (!IsValid())
    return 0.0f;
  __try {
    return *reinterpret_cast<float *>(m_address +
                                      m_offsets->HoverTransformRatioLerp);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0.0f;
  }
}

uint8_t CVehicle::GetLightsBroken() const {
  if (!IsValid())
    return 0;
  __try {
    return *reinterpret_cast<uint8_t *>(m_address + m_offsets->LightsBroken);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
}

void CHandlingData::SetDriveForce(float force) {
  if (!IsValid() || m_offsets->DriveForce == 0)
    return;
  __try {
    *reinterpret_cast<float *>(m_address + m_offsets->DriveForce) = force;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

uint8_t CVehicle::GetLightsVisuallyBroken() const {
  if (!IsValid())
    return 0;
  __try {
    return *reinterpret_cast<uint8_t *>(m_address +
                                        m_offsets->LightsVisuallyBroken);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
}

void CVehicle::SetGear(uint8_t gear) {
  if (!IsValid() || m_offsets->Gear == 0)
    return;
  __try {
    *reinterpret_cast<uint8_t *>(m_address + m_offsets->Gear) = gear;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

void CVehicle::SetNextGear(uint8_t gear) {
  if (!IsValid() || m_offsets->NextGear == 0)
    return;
  __try {
    *reinterpret_cast<uint8_t *>(m_address + m_offsets->NextGear) = gear;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

void CVehicle::SetTopGear(uint8_t gear) {
  if (!IsValid() || m_offsets->TopGear == 0)
    return;
  __try {
    *reinterpret_cast<uint8_t *>(m_address + m_offsets->TopGear) = gear;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

void CVehicle::SetClutch(float clutch) {
  if (!IsValid() || m_offsets->Clutch == 0)
    return;
  __try {
    *reinterpret_cast<float *>(m_address + m_offsets->Clutch) = clutch;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

void CVehicle::SetRPM(float rpm) {
  if (!IsValid() || m_offsets->RPM == 0)
    return;
  __try {
    *reinterpret_cast<float *>(m_address + m_offsets->RPM) = rpm;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

void CVehicle::SetThrottle(float throttle) {
  if (!IsValid() || m_offsets->Throttle == 0)
    return;
  __try {
    *reinterpret_cast<float *>(m_address + m_offsets->Throttle) = throttle;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

void CVehicle::SetThrottlePedal(float throttle) {
  if (!IsValid() || m_offsets->ThrottlePedal == 0)
    return;
  __try {
    *reinterpret_cast<float *>(m_address + m_offsets->ThrottlePedal) = throttle;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

void CVehicle::SetLightsBroken(uint8_t state) {
  if (!IsValid() || m_offsets->LightsBroken == 0)
    return;
  __try {
    *reinterpret_cast<uint8_t *>(m_address + m_offsets->LightsBroken) = state;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

void CVehicle::SetLightsVisuallyBroken(uint8_t state) {
  if (!IsValid() || m_offsets->LightsVisuallyBroken == 0)
    return;
  __try {
    *reinterpret_cast<uint8_t *>(m_address + m_offsets->LightsVisuallyBroken) =
        state;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

} // namespace GameMemory
