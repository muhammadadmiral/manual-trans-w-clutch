#pragma once

#include <windows.h>
#include <cstdint>

// Offset values for GTA V 1.0.1013.20
// These offsets might need tuning depending on the exact build.
struct VehicleOffsets {
    uint32_t Gear = 0x7EA;      // uint8_t
    uint32_t NextGear = 0x7E8;  // uint8_t
    uint32_t Clutch = 0x7F0;    // float
    uint32_t RPM = 0x7F4;       // float
};

class VehicleData {
private:
    BYTE* baseAddress;
    VehicleOffsets offsets;

public:
    VehicleData(int vehicleHandle);

    bool IsValid() const;

    // Getters
    uint8_t GetGear() const;
    uint8_t GetNextGear() const;
    float GetClutch() const;
    float GetRPM() const;

    // Setters
    void SetGear(uint8_t gear);
    void SetNextGear(uint8_t gear);
    void SetClutch(float clutch);
    void SetRPM(float rpm);
};
