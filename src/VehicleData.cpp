#include "VehicleData.h"
#include "../sdk/inc/main.h"

VehicleData::VehicleData(int vehicleHandle) {
    baseAddress = getScriptHandleBaseAddress(vehicleHandle);
    
    // Read offsets from .ini file if it exists, otherwise fallback to defaults
    char iniPath[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, iniPath);
    strcat_s(iniPath, "\\manual-trans.ini");

    // Helper function to read hex from INI
    auto ReadHexOffset = [](const char* key, uint32_t defaultVal, const char* iniPath) -> uint32_t {
        char buffer[32];
        if (GetPrivateProfileStringA("Offsets", key, "", buffer, sizeof(buffer), iniPath)) {
            if (strlen(buffer) > 0) {
                return (uint32_t)strtoul(buffer, nullptr, 16);
            }
        }
        return defaultVal;
    };

    offsets.Gear = ReadHexOffset("Gear", offsets.Gear, iniPath);
    offsets.NextGear = ReadHexOffset("NextGear", offsets.NextGear, iniPath);
    offsets.Clutch = ReadHexOffset("Clutch", offsets.Clutch, iniPath);
    offsets.RPM = ReadHexOffset("RPM", offsets.RPM, iniPath);
}

bool VehicleData::IsValid() const {
    return baseAddress != nullptr;
}

uint8_t VehicleData::GetGear() const {
    if (!IsValid()) return 0;
    return *reinterpret_cast<uint8_t*>(baseAddress + offsets.Gear);
}

uint8_t VehicleData::GetNextGear() const {
    if (!IsValid()) return 0;
    return *reinterpret_cast<uint8_t*>(baseAddress + offsets.NextGear);
}

float VehicleData::GetClutch() const {
    if (!IsValid()) return 0.0f;
    return *reinterpret_cast<float*>(baseAddress + offsets.Clutch);
}

float VehicleData::GetRPM() const {
    if (!IsValid()) return 0.0f;
    return *reinterpret_cast<float*>(baseAddress + offsets.RPM);
}

void VehicleData::SetGear(uint8_t gear) {
    if (IsValid()) {
        *reinterpret_cast<uint8_t*>(baseAddress + offsets.Gear) = gear;
    }
}

void VehicleData::SetNextGear(uint8_t gear) {
    if (IsValid()) {
        *reinterpret_cast<uint8_t*>(baseAddress + offsets.NextGear) = gear;
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
