#include "GameMemoryEngine.h"
#include "../../sdk/inc/main.h" // For getScriptHandleBaseAddress
#include "AOBScanner.h"
#include "OffsetResolver.h"

namespace GameMemory {



// --- CHandlingData Implementation ---
float CHandlingData::GetGearRatio(uint8_t gearIndex) const {
  if (!IsValid() || gearIndex > 32)
    return 0.0f;

  // In GTA V, GearRatios is a pointer to an array of floats inside
  // CHandlingData. Wait, no, GearRatios is inside CVehicleHandlingData, or
  // CHandlingData! Ikt's pattern for GearRatios was: `baseGearOffset + 8` from
  // the Gear pattern. Which actually resolves directly to the GearRatios array
  // pointer!

  uintptr_t ratiosArrayPtr = 0;
  // If the offset is in CVehicle, we can read the pointer directly.
  __try {
    ratiosArrayPtr =
        *reinterpret_cast<uintptr_t *>(m_address + m_offsets->GearRatios);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0.0f;
  }

  if (ratiosArrayPtr == 0)
    return 0.0f;

  __try {
    return *reinterpret_cast<float *>(ratiosArrayPtr +
                                      (gearIndex * sizeof(float)));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0.0f;
  }
}

float CHandlingData::GetDriveForce() const {
  if (!IsValid())
    return 0.0f;
  __try {
    // DriveForce is usually a float directly inside CHandlingData.
    // The pattern in OffsetResolver found the displacement for DriveForce.
    // Is it in CVehicle or CHandlingData?
    // If the instruction was `movss xmm0, [rcx+offset]`, it depends on what rcx
    // was. Assuming the offset is from CVehicle for now.
    return *reinterpret_cast<float *>(m_address + m_offsets->DriveForce);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0.0f;
  }
}

// --- CVehicle Implementation ---
CHandlingData CVehicle::GetHandlingData() const {
  // If we had the pointer offset to CHandlingData, we would traverse it here.
  // For now, we pass the vehicle address and let CHandlingData use the offsets.
  return CHandlingData(m_address, m_offsets);
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

float CVehicle::GetClutch() const {
  if (!IsValid())
    return 0.0f;
  __try {
    return *reinterpret_cast<float *>(m_address + m_offsets->Clutch);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0.0f;
  }
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
