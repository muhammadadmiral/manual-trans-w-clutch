#include "VehicleData.h"

#include "AOBScanner.h"
#include "../sdk/inc/main.h"

#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

VehicleOffsets VehicleData::resolvedOffsets{};
VehicleOffsetSource VehicleData::offsetSource =
    VehicleOffsetSource::Uninitialized;
bool VehicleData::initialized = false;

namespace {

constexpr char kGearPattern[] =
    "A8 02 0F 84 ? ? ? ? 0F B7 86";
constexpr ptrdiff_t kNextGearDisplacement = 11;
constexpr ptrdiff_t kCurrentGearDisplacement = 18;

constexpr char kRpmPattern[] =
    "F6 83 ? ? ? ? 07 75 ? 44 0F";
constexpr ptrdiff_t kRpmDisplacement = -42;

bool IsWritableProtection(DWORD protection) {
    if ((protection & PAGE_GUARD) != 0 ||
        (protection & PAGE_NOACCESS) != 0) {
        return false;
    }

    const DWORD baseProtection = protection & 0xFF;
    return baseProtection == PAGE_READWRITE ||
           baseProtection == PAGE_WRITECOPY ||
           baseProtection == PAGE_EXECUTE_READWRITE ||
           baseProtection == PAGE_EXECUTE_WRITECOPY;
}

bool IsWritableAddress(uintptr_t address, size_t size) {
    if (address == 0 || size == 0) return false;

    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &info,
                     sizeof(info)) == 0) {
        return false;
    }

    if (info.State != MEM_COMMIT || !IsWritableProtection(info.Protect)) {
        return false;
    }

    const uintptr_t start = reinterpret_cast<uintptr_t>(info.BaseAddress);
    const uintptr_t end = start + info.RegionSize;
    if (address < start || address > end) return false;
    return size <= end - address;
}

bool TryReadU32(uintptr_t address, uint32_t& value) {
    if (!AOBScanner::IsReadable(address, sizeof(value))) return false;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return true;
}

bool BuildIniPath(HMODULE pluginModule, char (&path)[MAX_PATH]) {
    DWORD length = 0;
    if (pluginModule != nullptr) {
        length = GetModuleFileNameA(pluginModule, path, MAX_PATH);
    }

    if (length == 0 || length >= MAX_PATH) {
        length = GetCurrentDirectoryA(MAX_PATH, path);
        if (length == 0 || length >= MAX_PATH) return false;
    } else {
        char* slash = std::strrchr(path, '\\');
        if (slash == nullptr) slash = std::strrchr(path, '/');
        if (slash == nullptr) return false;
        *slash = '\0';
    }

    return strcat_s(path, "\\manual-trans.ini") == 0;
}

uint32_t ReadHexOffset(const char* iniPath, const char* key) {
    char buffer[32]{};
    const DWORD count = GetPrivateProfileStringA(
        "Offsets", key, "", buffer, sizeof(buffer), iniPath);
    if (count == 0) return 0;

    char* end = nullptr;
    const unsigned long value = std::strtoul(buffer, &end, 0);
    if (end == buffer || *end != '\0' ||
        value > std::numeric_limits<uint32_t>::max()) {
        return 0;
    }

    return static_cast<uint32_t>(value);
}

} // namespace

bool VehicleOffsets::IsComplete() const {
    return Gear != 0 && NextGear != 0 && Clutch != 0 && RPM != 0;
}

bool VehicleData::AreOffsetsSane(const VehicleOffsets& value) {
    if (!value.IsComplete()) return false;

    constexpr uint32_t kMinimumOffset = 0x100;
    constexpr uint32_t kMaximumOffset = 0x8000;
    const uint32_t fields[] = {
        value.Gear, value.NextGear, value.Clutch, value.RPM
    };

    for (const uint32_t field : fields) {
        if (field < kMinimumOffset || field >= kMaximumOffset) return false;
    }

    const uint32_t gearDistance = value.Gear > value.NextGear
        ? value.Gear - value.NextGear
        : value.NextGear - value.Gear;

    if (value.Gear == value.NextGear || gearDistance > 0x20) return false;
    const uint32_t driveDistance = value.Clutch > value.RPM
        ? value.Clutch - value.RPM
        : value.RPM - value.Clutch;
    if (value.Clutch == value.RPM || driveDistance > 0x40) return false;
    if ((value.RPM & 0x3) != 0 || (value.Clutch & 0x3) != 0) return false;

    return true;
}

bool VehicleData::ResolveOffsetsByPattern(VehicleOffsets& result) {
    const uintptr_t gearMatch = AOBScanner::FindUnique(kGearPattern);
    const uintptr_t rpmMatch = AOBScanner::FindUnique(kRpmPattern);
    if (gearMatch == 0 || rpmMatch == 0 ||
        rpmMatch < static_cast<uintptr_t>(-kRpmDisplacement)) {
        return false;
    }

    VehicleOffsets candidate{};
    if (!TryReadU32(gearMatch + kCurrentGearDisplacement,
                    candidate.Gear) ||
        !TryReadU32(gearMatch + kNextGearDisplacement,
                    candidate.NextGear) ||
        !TryReadU32(rpmMatch - static_cast<uintptr_t>(-kRpmDisplacement),
                    candidate.RPM)) {
        return false;
    }

    candidate.Clutch = candidate.RPM + 12;
    if (!AreOffsetsSane(candidate)) return false;

    result = candidate;
    return true;
}

bool VehicleData::LoadOffsetsFromIni(HMODULE pluginModule,
                                     VehicleOffsets& result) {
    char iniPath[MAX_PATH]{};
    if (!BuildIniPath(pluginModule, iniPath)) return false;

    VehicleOffsets candidate{};
    candidate.Gear = ReadHexOffset(iniPath, "Gear");
    candidate.NextGear = ReadHexOffset(iniPath, "NextGear");
    candidate.Clutch = ReadHexOffset(iniPath, "Clutch");
    candidate.RPM = ReadHexOffset(iniPath, "RPM");

    if (!AreOffsetsSane(candidate)) return false;
    result = candidate;
    return true;
}

bool VehicleData::Initialize(HMODULE pluginModule) {
    if (initialized) return true;

    VehicleOffsets candidate{};
    if (ResolveOffsetsByPattern(candidate)) {
        resolvedOffsets = candidate;
        offsetSource = VehicleOffsetSource::PatternScan;
        initialized = true;
        return true;
    }

    char iniPath[MAX_PATH]{};
    const bool hasIniPath = BuildIniPath(pluginModule, iniPath);
    const bool allowFallback = hasIniPath &&
        GetPrivateProfileIntA("Memory", "AllowIniFallback", 0,
                              iniPath) != 0;

    if (allowFallback && LoadOffsetsFromIni(pluginModule, candidate)) {
        resolvedOffsets = candidate;
        offsetSource = VehicleOffsetSource::IniFallback;
        initialized = true;
        return true;
    }

    resolvedOffsets = {};
    offsetSource = VehicleOffsetSource::Uninitialized;
    initialized = false;
    return false;
}

bool VehicleData::IsInitialized() {
    return initialized;
}

VehicleOffsetSource VehicleData::GetOffsetSource() {
    return offsetSource;
}

const char* VehicleData::GetOffsetSourceName() {
    switch (offsetSource) {
    case VehicleOffsetSource::PatternScan:
        return "AOB";
    case VehicleOffsetSource::IniFallback:
        return "INI fallback";
    default:
        return "unresolved";
    }
}

const VehicleOffsets& VehicleData::GetResolvedOffsets() {
    return resolvedOffsets;
}

VehicleData::VehicleData(int vehicleHandle) {
    if (!initialized) return;
    baseAddress = getScriptHandleBaseAddress(vehicleHandle);
}

bool VehicleData::CanRead(uint32_t offset, size_t size) const {
    if (!initialized || baseAddress == nullptr || offset == 0) return false;
    return AOBScanner::IsReadable(
        reinterpret_cast<uintptr_t>(baseAddress) + offset, size);
}

bool VehicleData::CanWrite(uint32_t offset, size_t size) const {
    if (!initialized || baseAddress == nullptr || offset == 0) return false;
    return IsWritableAddress(
        reinterpret_cast<uintptr_t>(baseAddress) + offset, size);
}

bool VehicleData::IsValid() const {
    return CanRead(resolvedOffsets.Gear, sizeof(uint8_t)) &&
           CanRead(resolvedOffsets.NextGear, sizeof(uint8_t)) &&
           CanRead(resolvedOffsets.Clutch, sizeof(float)) &&
           CanRead(resolvedOffsets.RPM, sizeof(float)) &&
           CanWrite(resolvedOffsets.Gear, sizeof(uint8_t)) &&
           CanWrite(resolvedOffsets.NextGear, sizeof(uint8_t));
}

uint8_t VehicleData::GetGear() const {
    if (!CanRead(resolvedOffsets.Gear, sizeof(uint8_t))) return 0;
    return *reinterpret_cast<const uint8_t*>(
        baseAddress + resolvedOffsets.Gear);
}

uint8_t VehicleData::GetNextGear() const {
    if (!CanRead(resolvedOffsets.NextGear, sizeof(uint8_t))) return 0;
    return *reinterpret_cast<const uint8_t*>(
        baseAddress + resolvedOffsets.NextGear);
}

float VehicleData::GetClutch() const {
    if (!CanRead(resolvedOffsets.Clutch, sizeof(float))) return 0.0f;
    return *reinterpret_cast<const float*>(
        baseAddress + resolvedOffsets.Clutch);
}

float VehicleData::GetRPM() const {
    if (!CanRead(resolvedOffsets.RPM, sizeof(float))) return 0.0f;
    return *reinterpret_cast<const float*>(
        baseAddress + resolvedOffsets.RPM);
}

bool VehicleData::HasPlausibleLayout(int maxGear) const {
    if (!IsValid() || maxGear < 1 || maxGear > 32) return false;

    const uint8_t gear = GetGear();
    const uint8_t nextGear = GetNextGear();
    const float clutch = GetClutch();
    const float rpm = GetRPM();

    const bool gearPlausible =
        gear <= static_cast<uint8_t>(maxGear + 1) || gear == 0xFF;
    const bool nextGearPlausible =
        nextGear <= static_cast<uint8_t>(maxGear + 1) || nextGear == 0xFF;

    return gearPlausible && nextGearPlausible &&
           std::isfinite(clutch) && clutch >= -0.25f && clutch <= 2.0f &&
           std::isfinite(rpm) && rpm >= -0.25f && rpm <= 2.5f;
}

bool VehicleData::SetGear(uint8_t gear) {
    if (!CanWrite(resolvedOffsets.Gear, sizeof(gear))) return false;
    *reinterpret_cast<uint8_t*>(baseAddress + resolvedOffsets.Gear) = gear;
    return true;
}

bool VehicleData::SetNextGear(uint8_t gear) {
    if (!CanWrite(resolvedOffsets.NextGear, sizeof(gear))) return false;
    *reinterpret_cast<uint8_t*>(baseAddress + resolvedOffsets.NextGear) = gear;
    return true;
}

bool VehicleData::SetClutch(float clutch) {
    if (!std::isfinite(clutch) ||
        !CanWrite(resolvedOffsets.Clutch, sizeof(clutch))) {
        return false;
    }
    *reinterpret_cast<float*>(baseAddress + resolvedOffsets.Clutch) = clutch;
    return true;
}

bool VehicleData::SetRPM(float rpm) {
    if (!std::isfinite(rpm) ||
        !CanWrite(resolvedOffsets.RPM, sizeof(rpm))) {
        return false;
    }
    *reinterpret_cast<float*>(baseAddress + resolvedOffsets.RPM) = rpm;
    return true;
}
