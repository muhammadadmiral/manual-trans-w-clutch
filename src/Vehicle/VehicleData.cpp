// =============================================================================
// VehicleData.cpp
// Offset resolution (AOB → INI → calibration) + per-vehicle instance methods.
//
// ── What belongs here ─────────────────────────────────────────────────────────
//   • Static data definitions (resolvedOffsets, initialized, …)
//   • Initialize()        — AOB → INI → launch calibration
//   • UpdateCalibration() — thin wrapper; delegates to CalibrationEngine
//   • ResetCalibration()
//   • SaveOffsetsToIni / LoadOffsetsFromIni / AreOffsetsSane
//   • Per-vehicle instance methods (GetRPM, SetGear, HasPlausibleLayout, …)
//
// ── What does NOT belong here (moved to CalibrationEngine.cpp) ────────────────
//   • The calibration state machine phases
//   • SearchGearLayout (two-pass robust scan)
//   • Per-phase timing constants
// =============================================================================
#include "VehicleData.h"

#include "../../sdk/inc/main.h"
#include "../Memory/AOBScanner.h"
#include "../Memory/CalibrationEngine.h"
#include "../Memory/OffsetResolver.h"
#include "../Core/ModLogger.h"

#define NOMINMAX
#include <Windows.h>
#include <winver.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#pragma comment(lib, "Version.lib")

// =============================================================================
// Static data
// =============================================================================
VehicleOffsets      VehicleData::resolvedOffsets{};
VehicleOffsetSource VehicleData::offsetSource = VehicleOffsetSource::Uninitialized;
bool                VehicleData::initialized  = false;
std::string         VehicleData::lastFailureReason = "not initialized";

// =============================================================================
// Anonymous helpers
// =============================================================================
namespace {

bool IsWritableProtection(DWORD protection) {
    if ((protection & PAGE_GUARD) || (protection & PAGE_NOACCESS)) return false;
    const DWORD base = protection & 0xFF;
    return base == PAGE_READWRITE     || base == PAGE_WRITECOPY ||
           base == PAGE_EXECUTE_READWRITE || base == PAGE_EXECUTE_WRITECOPY;
}

bool IsWritableAddress(uintptr_t address, size_t size) {
    if (!address || !size) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)))
        return false;
    if (info.State != MEM_COMMIT || !IsWritableProtection(info.Protect))
        return false;
    const uintptr_t start = reinterpret_cast<uintptr_t>(info.BaseAddress);
    const uintptr_t end   = start + info.RegionSize;
    return address >= start && size <= (end - address);
}

bool TryReadU32(uintptr_t address, uint32_t& value) {
    if (!AOBScanner::IsReadable(address, sizeof(value))) return false;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return true;
}

// Build the absolute path to "manual-trans.ini" next to the .asi.
bool BuildIniPath(HMODULE pluginModule, char (&path)[MAX_PATH]) {
    DWORD len = 0;
    if (pluginModule)
        len = GetModuleFileNameA(pluginModule, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        len = GetCurrentDirectoryA(MAX_PATH, path);
    if (len == 0 || len >= MAX_PATH)
        return false;

    if (pluginModule) {
        // Strip filename → keep directory
        char* slash = std::strrchr(path, '\\');
        if (!slash) slash = std::strrchr(path, '/');
        if (!slash) return false;
        *slash = '\0';
    }
    return strcat_s(path, "\\manual-trans.ini") == 0;
}

uint32_t ReadHexOffset(const char* iniPath, const char* section, const char* key) {
    char buffer[32]{};
    if (!GetPrivateProfileStringA(section, key, "", buffer, sizeof(buffer), iniPath))
        return 0;
    char* end = nullptr;
    const unsigned long v = std::strtoul(buffer, &end, 0);
    if (end == buffer || *end != '\0' || v > (std::numeric_limits<uint32_t>::max)())
        return 0;
    return static_cast<uint32_t>(v);
}

// Read the 4-part file version of the given module (e.g. "1.0.3274.0").
bool GetModuleFileVersion(HMODULE module, std::string& out) {
    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(module, path, MAX_PATH)) return false;
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeA(path, &handle);
    if (!size) return false;
    std::vector<char> buf(size);
    if (!GetFileVersionInfoA(path, 0, size, buf.data())) return false;
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT fiSize = 0;
    if (!VerQueryValueA(buf.data(), "\\", reinterpret_cast<void**>(&fi), &fiSize) || !fi)
        return false;
    char text[64]{};
    sprintf_s(text, "%u.%u.%u.%u",
              HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS),
              HIWORD(fi->dwFileVersionLS), LOWORD(fi->dwFileVersionLS));
    out = text;
    return true;
}

} // namespace

// =============================================================================
// Static API — offset resolution
// =============================================================================

std::string VehicleData::GetGameBuildVersion() {
    std::string v;
    GetModuleFileVersion(GetModuleHandleW(nullptr), v);
    return v;
}

bool VehicleData::AreOffsetsSane(const VehicleOffsets& v) {
    if (!v.IsCompleteCore()) return false;

    constexpr uint32_t kMin = 0x100, kMax = 0x8000;
    const uint32_t fields[] = {v.Gear, v.NextGear, v.RPM};
    for (uint32_t f : fields)
        if (f < kMin || f >= kMax) return false;

    const uint32_t gDist = v.Gear > v.NextGear ? v.Gear - v.NextGear
                                                : v.NextGear - v.Gear;
    if (gDist == 0 || gDist > 0x20) return false;
    if (v.Clutch != v.RPM + 0xC) return false;

    if ((v.RPM & 3) != 0) return false;
    if (v.Clutch != 0 &&
        (v.Clutch < kMin || v.Clutch >= kMax || (v.Clutch & 3) != 0))
        return false;
    if (v.GearRatios != 0 &&
        (v.GearRatios < kMin || v.GearRatios >= kMax ||
         (v.GearRatios & 3) != 0))
        return false;
    return true;
}

bool VehicleData::ResolveOffsetsByPattern(VehicleOffsets& result) {
    std::string failReason;
    if (!OffsetResolver::ScanPatterns(result, failReason)) {
        lastFailureReason = failReason;
        return false;
    }
    if (!AreOffsetsSane(result)) {
        lastFailureReason = "pattern offsets failed sanity check (game build mismatch?)";
        return false;
    }
    return true;
}

// =============================================================================
// INI persistence  (LoadOffsetsFromIni / SaveOffsetsToIni)
// =============================================================================

bool VehicleData::LoadOffsetsFromIni(HMODULE pluginModule, VehicleOffsets& result) {
    char iniPath[MAX_PATH]{};
    if (!BuildIniPath(pluginModule, iniPath)) return false;
    if (!GetPrivateProfileIntA("Memory", "AllowIniFallback", 1, iniPath)) return false;

    auto readAll = [&](const char* section, VehicleOffsets& out) {
        out.Gear                  = ReadHexOffset(iniPath, section, "Gear");
        out.NextGear              = ReadHexOffset(iniPath, section, "NextGear");
        out.Clutch                = ReadHexOffset(iniPath, section, "Clutch");
        out.RPM                   = ReadHexOffset(iniPath, section, "RPM");
        out.Throttle              = ReadHexOffset(iniPath, section, "Throttle");
        out.ThrottlePedal         = ReadHexOffset(iniPath, section, "ThrottlePedal");
        out.HandlingPtr           = ReadHexOffset(iniPath, section, "HandlingPtr");
        out.DriveInertia          = ReadHexOffset(iniPath, section, "DriveInertia");
        out.DriveMaxFlatVel       = ReadHexOffset(iniPath, section, "DriveMaxFlatVel");
        out.WheelsPtr             = ReadHexOffset(iniPath, section, "WheelsPtr");
        out.WheelCount            = ReadHexOffset(iniPath, section, "WheelCount");
        out.WheelAngularVelocity  = ReadHexOffset(iniPath, section, "WheelAngularVelocity");
        out.WheelLoad             = ReadHexOffset(iniPath, section, "WheelLoad");
        out.WheelBrakePressure    = ReadHexOffset(iniPath, section, "WheelBrakePressure");
        out.WheelPower            = ReadHexOffset(iniPath, section, "WheelPower");
        out.TopGear               = ReadHexOffset(iniPath, section, "TopGear");
        out.DriveForce            = ReadHexOffset(iniPath, section, "DriveForce");
        out.FuelLevel             = ReadHexOffset(iniPath, section, "FuelLevel");
        out.LightsBroken          = ReadHexOffset(iniPath, section, "LightsBroken");
        out.LightsVisuallyBroken  = ReadHexOffset(iniPath, section, "LightsVisuallyBroken");
        out.HoverTransformRatioLerp = ReadHexOffset(iniPath, section, "HoverTransformRatioLerp");
        out.GearRatios            = ReadHexOffset(iniPath, section, "GearRatios");
        out.GearRatiosInline      = ReadHexOffset(iniPath, section, "GearRatiosInline");
        
        // Backwards compatibility for old INIs that didn't save TopGear
        if (out.TopGear == 0 && out.NextGear != 0) {
            out.TopGear = out.NextGear + 6;
        }

        if (out.RPM != 0 && out.Clutch != out.RPM + 0xC) {
            LOG_WARN(Memory,
                     "Correcting clutch relation: 0x%X -> 0x%X",
                     out.Clutch, out.RPM + 0xC);
            out.Clutch = out.RPM + 0xC;
        }
        if (out.RPM != 0 && out.Throttle != out.RPM + 0x10) {
            LOG_WARN(Memory,
                     "Correcting engine throttle relation: 0x%X -> 0x%X",
                     out.Throttle, out.RPM + 0x10);
            out.Throttle = out.RPM + 0x10;
        }
        if (out.GearRatios == 0 && out.NextGear != 0 &&
            out.Gear == out.NextGear + 2 &&
            out.TopGear == out.NextGear + 6) {
            out.GearRatios = out.NextGear + 0xC;
            out.GearRatiosInline = 1;
            LOG_INFO(Memory,
                     "Migrated inline gear ratios to 0x%X from gear cluster",
                     out.GearRatios);
        }
        OffsetResolver::EnrichOptionalOffsets(out);
    };

    // Prefer version-specific section (e.g. [Offsets.1.0.3274.0])
    const std::string buildVer = GetGameBuildVersion();
    if (!buildVer.empty()) {
        const std::string sec = "Offsets." + buildVer;
        VehicleOffsets v{};
        readAll(sec.c_str(), v);
        if (AreOffsetsSane(v)) {
            result = v;
            LOG_INFO(Memory, "INI versioned section [%s] loaded", sec.c_str());
            return true;
        }
    }

    VehicleOffsets v{};
    readAll("Offsets", v);
    if (!AreOffsetsSane(v)) return false;
    result = v;
    LOG_INFO(Memory, "INI generic [Offsets] section loaded");
    return true;
}

void VehicleData::SaveOffsetsToIni(HMODULE pluginModule,
                                   const VehicleOffsets& offsets) {
    char iniPath[MAX_PATH]{};
    if (!BuildIniPath(pluginModule, iniPath)) {
        LOG_ERROR(Memory, "SaveOffsetsToIni: BuildIniPath failed — offsets NOT saved");
        return;
    }

    char buf[32]{};
    auto write = [&](const char* section, const char* key, uint32_t value) {
        sprintf_s(buf, "0x%X", value);
        WritePrivateProfileStringA(section, key, buf, iniPath);
    };

    auto writeAll = [&](const char* section) {
        write(section, "Gear",                   offsets.Gear);
        write(section, "NextGear",               offsets.NextGear);
        write(section, "Clutch",                 offsets.Clutch);
        write(section, "RPM",                    offsets.RPM);
        write(section, "Throttle",               offsets.Throttle);
        write(section, "ThrottlePedal",          offsets.ThrottlePedal);
        write(section, "HandlingPtr",             offsets.HandlingPtr);
        write(section, "DriveInertia",            offsets.DriveInertia);
        write(section, "DriveMaxFlatVel",         offsets.DriveMaxFlatVel);
        write(section, "WheelsPtr",               offsets.WheelsPtr);
        write(section, "WheelCount",              offsets.WheelCount);
        write(section, "WheelAngularVelocity",    offsets.WheelAngularVelocity);
        write(section, "WheelLoad",               offsets.WheelLoad);
        write(section, "WheelBrakePressure",      offsets.WheelBrakePressure);
        write(section, "WheelPower",              offsets.WheelPower);
        write(section, "TopGear",                offsets.TopGear);
        write(section, "DriveForce",             offsets.DriveForce);
        write(section, "FuelLevel",              offsets.FuelLevel);
        write(section, "LightsBroken",           offsets.LightsBroken);
        write(section, "LightsVisuallyBroken",   offsets.LightsVisuallyBroken);
        write(section, "HoverTransformRatioLerp",offsets.HoverTransformRatioLerp);
        write(section, "GearRatios",             offsets.GearRatios);
        write(section, "GearRatiosInline",       offsets.GearRatiosInline);
    };

    writeAll("Offsets");

    const std::string buildVer = GetGameBuildVersion();
    if (!buildVer.empty())
        writeAll(("Offsets." + buildVer).c_str());

    WritePrivateProfileStringA("Memory", "AllowIniFallback", "1", iniPath);
    LOG_INFO(Memory, "Offsets saved → G=0x%X N=0x%X RPM=0x%X CLT=0x%X [build:%s]",
             offsets.Gear, offsets.NextGear, offsets.RPM, offsets.Clutch,
             buildVer.empty() ? "?" : buildVer.c_str());
}

// =============================================================================
// Initialize — AOB scan → INI fallback → launch interactive calibration
// =============================================================================
bool VehicleData::Initialize(HMODULE pluginModule) {
    if (initialized) return true;

    LOG_INFO(Init, "VehicleData::Initialize — trying AOB pattern scan...");
    VehicleOffsets candidate{};

    if (ResolveOffsetsByPattern(candidate)) {
        resolvedOffsets = candidate;
        offsetSource    = VehicleOffsetSource::PatternScan;
        initialized     = true;
        LOG_INFO(Init, "AOB scan OK — G=0x%X N=0x%X RPM=0x%X CLT=0x%X",
                 candidate.Gear, candidate.NextGear, candidate.RPM, candidate.Clutch);
        SaveOffsetsToIni(pluginModule, candidate);
        return true;
    }

    LOG_WARN(Init, "AOB failed (%s). Trying INI fallback...", lastFailureReason.c_str());

    if (LoadOffsetsFromIni(pluginModule, candidate)) {
        resolvedOffsets = candidate;
        offsetSource    = VehicleOffsetSource::IniFallback;
        initialized     = true;
        LOG_INFO(Init, "INI fallback OK — G=0x%X N=0x%X RPM=0x%X CLT=0x%X",
                 candidate.Gear, candidate.NextGear, candidate.RPM, candidate.Clutch);
        SaveOffsetsToIni(pluginModule, candidate);
        return true;
    }

    LOG_WARN(Init, "INI unavailable. Starting interactive calibration.");
    offsetSource = VehicleOffsetSource::Calibration;
    initialized  = false;
    CalibrationEngine::Reset();  // sets state to WaitingForEngineOff
    return true;
}

// =============================================================================
// Calibration delegation
// =============================================================================
void VehicleData::UpdateCalibration(HMODULE pluginModule, int vehicleHandle,
                                    bool isEngineOn, bool isRevving, uint8_t maxGear) {
    if (initialized) return;

    VehicleOffsets offsets{};
    if (CalibrationEngine::Update(vehicleHandle, isEngineOn, isRevving, maxGear, offsets)) {
        resolvedOffsets = offsets;
        offsetSource    = VehicleOffsetSource::Calibration;
        initialized     = true;
        SaveOffsetsToIni(pluginModule, resolvedOffsets);
    }

    // Mirror calibration errors so VehicleData::GetLastFailureReason() always works.
    if (CalibrationEngine::GetState() == CalibrationState::Failed)
        lastFailureReason = CalibrationEngine::GetLastError();
}

void VehicleData::ResetCalibration() {
    LOG_INFO(Calib, "ResetCalibration: wiping resolved offsets and restarting");
    initialized     = false;
    offsetSource    = VehicleOffsetSource::Uninitialized;
    resolvedOffsets = {};
    CalibrationEngine::Reset();
}

CalibrationState VehicleData::GetCalibrationState()        { return CalibrationEngine::GetState(); }
size_t           VehicleData::GetCalibrationCandidateCount(){ return CalibrationEngine::GetCandidateCount(); }

// =============================================================================
// Static query API
// =============================================================================
bool                       VehicleData::IsInitialized()    { return initialized; }
VehicleOffsetSource        VehicleData::GetOffsetSource()  { return offsetSource; }
const VehicleOffsets&      VehicleData::GetResolvedOffsets(){ return resolvedOffsets; }
const std::string&         VehicleData::GetLastFailureReason() {
    if (CalibrationEngine::GetState() == CalibrationState::Failed &&
        !CalibrationEngine::GetLastError().empty())
        lastFailureReason = CalibrationEngine::GetLastError();
    return lastFailureReason;
}

const char* VehicleData::GetOffsetSourceName() {
    switch (offsetSource) {
        case VehicleOffsetSource::PatternScan: return "AOB";
        case VehicleOffsetSource::IniFallback: return "INI";
        case VehicleOffsetSource::Calibration: return "Calib";
        default:                               return "None";
    }
}

// =============================================================================
// Per-vehicle instance
// =============================================================================
VehicleData::VehicleData(int vehicleHandle)
    : m_vehicle(reinterpret_cast<uintptr_t>(getScriptHandleBaseAddress(vehicleHandle)),
                &resolvedOffsets),
      m_isValid(m_vehicle.IsValid())
{
    auto h = m_vehicle.GetHandlingData();
    if (h.IsValid()) {
        m_originalDriveForce = h.GetDriveForce();
    } else {
        m_originalDriveForce = -1.0f;
    }
}

bool VehicleData::CanRead (uint32_t offset, size_t) const { return m_isValid && offset != 0; }
bool VehicleData::CanWrite(uint32_t offset, size_t) const { return m_isValid && offset != 0; }
bool VehicleData::IsValid() const { return m_isValid; }

bool VehicleData::HasPlausibleLayout(int maxGear) const {
    if (!IsValid() || maxGear < 1 || maxGear > 32) return false;

    const uint8_t gear     = GetGear();
    const uint8_t nextGear = GetNextGear();
    const uint8_t topGear  = GetTopGear();
    const float   rpm      = GetRPM();
    const bool clutchOk =
        resolvedOffsets.Clutch == 0 ||
        (std::isfinite(GetClutch()) && GetClutch() >= -6.0f &&
         GetClutch() <= 2.0f);

    // 0xFF == invalid / neutral on some vehicles
    const bool gearOk     = gear     <= static_cast<uint8_t>(maxGear + 1) || gear     == 0xFF;
    const bool nextGearOk = nextGear <= static_cast<uint8_t>(maxGear + 1) || nextGear == 0xFF;
    const bool topGearOk =
        resolvedOffsets.TopGear == 0 ||
        topGear == static_cast<uint8_t>(maxGear);
    bool ratiosOk = true;
    if (resolvedOffsets.GearRatios != 0 && maxGear >= 2) {
        const float reverse = GetGearRatio(0);
        const float first = GetGearRatio(1);
        const float second = GetGearRatio(2);
        ratiosOk = std::isfinite(reverse) && std::isfinite(first) &&
                   std::isfinite(second) && reverse < -0.05f &&
                   first > second && second > 0.05f && first < 15.0f;
    }

    return gearOk && nextGearOk && topGearOk && ratiosOk && clutchOk &&
           std::isfinite(rpm)    && rpm    >= -0.25f && rpm    <= 2.5f;
}

// ── Getters ──────────────────────────────────────────────────────────────────
uint8_t VehicleData::GetGear()               const { return m_vehicle.GetGear(); }
uint8_t VehicleData::GetNextGear()           const { return m_vehicle.GetNextGear(); }
uint8_t VehicleData::GetTopGear()            const { return m_vehicle.GetTopGear(); }
float   VehicleData::GetClutch()             const { return m_vehicle.GetClutch(); }
float   VehicleData::GetRPM()               const { return m_vehicle.GetRPM(); }
float   VehicleData::GetThrottle()          const { return m_vehicle.GetThrottle(); }
float   VehicleData::GetThrottlePedal()     const { return m_vehicle.GetThrottlePedal(); }
float   VehicleData::GetFuelLevel()          const { return m_vehicle.GetFuelLevel(); }
uint8_t VehicleData::GetLightsBroken()       const { return m_vehicle.GetLightsBroken(); }
uint8_t VehicleData::GetLightsVisuallyBroken() const { return m_vehicle.GetLightsVisuallyBroken(); }
float   VehicleData::GetHoverTransformRatioLerp() const { return m_vehicle.GetHoverTransformRatioLerp(); }

float VehicleData::GetDriveForce() const {
    if (!m_isValid) return 0.0f;
    auto h = m_vehicle.GetHandlingData();
    return h.IsValid() ? h.GetDriveForce() : 0.0f;
}

float VehicleData::GetDriveInertia() const {
    if (!m_isValid) return 0.0f;
    auto h = m_vehicle.GetHandlingData();
    return h.IsValid() ? h.GetDriveInertia() : 0.0f;
}

float VehicleData::GetDriveMaxFlatVel() const {
    if (!m_isValid) return 0.0f;
    auto h = m_vehicle.GetHandlingData();
    return h.IsValid() ? h.GetDriveMaxFlatVel() : 0.0f;
}

uint8_t VehicleData::GetWheelCount() const {
    return m_isValid ? m_vehicle.GetWheelCount() : 0;
}

GameMemory::WheelTelemetry
VehicleData::GetWheelTelemetry(uint8_t index) const {
    return m_isValid ? m_vehicle.GetWheelTelemetry(index)
                     : GameMemory::WheelTelemetry{};
}

float VehicleData::GetOriginalDriveForce() const {
    return m_originalDriveForce;
}

float VehicleData::GetGearRatio(uint8_t gear) const {
    if (!m_isValid) return 0.0f;
    return m_vehicle.GetGearRatio(gear);
}

// ── Setters ──────────────────────────────────────────────────────────────────
bool VehicleData::SetGear(uint8_t gear) {
    if (!m_isValid) return false;
    m_vehicle.SetGear(gear);
    return true;
}
bool VehicleData::SetNextGear(uint8_t gear) {
    if (!m_isValid) return false;
    m_vehicle.SetNextGear(gear);
    return true;
}
bool VehicleData::SetTopGear(uint8_t gear) {
    if (!m_isValid) return false;
    m_vehicle.SetTopGear(gear);
    return true;
}
bool VehicleData::SetClutch(float clutch) {
    if (!m_isValid || resolvedOffsets.Clutch == 0 ||
        !std::isfinite(clutch))
        return false;
    m_vehicle.SetClutch(clutch);
    return true;
}
bool VehicleData::SetRPM(float rpm) {
    if (!m_isValid || !std::isfinite(rpm)) return false;
    m_vehicle.SetRPM(rpm);
    return true;
}
bool VehicleData::SetThrottle(float throttle) {
    if (!m_isValid || !std::isfinite(throttle)) return false;
    m_vehicle.SetThrottle(throttle);
    return true;
}
bool VehicleData::SetThrottlePedal(float throttle) {
    if (!m_isValid || !std::isfinite(throttle)) return false;
    m_vehicle.SetThrottlePedal(throttle);
    return true;
}
bool VehicleData::SetDriveForce(float force) {
    if (!m_isValid) return false;
    auto h = m_vehicle.GetHandlingData();
    if (!h.IsValid()) return false;
    h.SetDriveForce(force);
    return true;
}
bool VehicleData::SetLightsBroken(uint8_t state) {
    if (!m_isValid) return false;
    m_vehicle.SetLightsBroken(state);
    return true;
}
bool VehicleData::SetLightsVisuallyBroken(uint8_t state) {
    if (!m_isValid) return false;
    m_vehicle.SetLightsVisuallyBroken(state);
    return true;
}
