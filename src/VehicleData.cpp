#include "VehicleData.h"
#include "../sdk/inc/main.h"

VehicleData::VehicleData(int vehicleHandle) {
    // getScriptHandleBaseAddress returns BYTE* to the CVehicle structure in memory
    baseAddress = getScriptHandleBaseAddress(vehicleHandle);
}

bool VehicleData::IsValid() const {
    return baseAddress != nullptr;
}

uint16_t VehicleData::GetGear() const {
    if (!IsValid()) return 0;
    return *reinterpret_cast<uint16_t*>(baseAddress + offsets.Gear);
}

uint16_t VehicleData::GetNextGear() const {
    if (!IsValid()) return 0;
    return *reinterpret_cast<uint16_t*>(baseAddress + offsets.NextGear);
}

float VehicleData::GetClutch() const {
    if (!IsValid()) return 0.0f;
    return *reinterpret_cast<float*>(baseAddress + offsets.Clutch);
}

float VehicleData::GetRPM() const {
    if (!IsValid()) return 0.0f;
    return *reinterpret_cast<float*>(baseAddress + offsets.RPM);
}

void VehicleData::SetGear(uint16_t gear) {
    if (IsValid()) {
        *reinterpret_cast<uint16_t*>(baseAddress + offsets.Gear) = gear;
    }
}

void VehicleData::SetNextGear(uint16_t gear) {
    if (IsValid()) {
        *reinterpret_cast<uint16_t*>(baseAddress + offsets.NextGear) = gear;
    }
}

void VehicleData::SetClutch(float clutch) {
    if (IsValid()) {
        *reinterpret_cast<float*>(baseAddress + offsets.Clutch) = clutch;
    }
}

void VehicleData::SetRPM(float rpm) {
    if (IsValid()) {
        *reinterpret_cast<float*>(baseAddress + offsets.RPM) = rpm;
    }
}
