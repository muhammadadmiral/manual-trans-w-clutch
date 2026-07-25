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
    const uint32_t fields[] = {v.Gear, v.NextGear, v.Clutch, v.RPM};
    for (uint32_t f : fields)
        if (f < kMin || f >= kMax) return false;

    const uint32_t gDist = v.Gear > v.NextGear ? v.Gear - v.NextGear
                                                : v.NextGear - v.Gear;
    if (gDist == 0 || gDist > 0x20) return false;

    const uint32_t cDist = v.Clutch > v.RPM ? v.Clutch - v.RPM
                                              : v.RPM - v.Clutch;
    if (cDist == 0 || cDist > 0x40) return false;

    if ((v.RPM & 3) || (v.Clutch & 3)) return false;
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
        out.TopGear               = ReadHexOffset(iniPath, section, "TopGear");
        out.DriveForce            = ReadHexOffset(iniPath, section, "DriveForce");
        out.FuelLevel             = ReadHexOffset(iniPath, section, "FuelLevel");
        out.LightsBroken          = ReadHexOffset(iniPath, section, "LightsBroken");
        out.LightsVisuallyBroken  = ReadHexOffset(iniPath, section, "LightsVisuallyBroken");
        out.HoverTransformRatioLerp = ReadHexOffset(iniPath, section, "HoverTransformRatioLerp");
        out.GearRatios            = ReadHexOffset(iniPath, section, "GearRatios");
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
        write(section, "TopGear",                offsets.TopGear);
        write(section, "DriveForce",             offsets.DriveForce);
        write(section, "FuelLevel",              offsets.FuelLevel);
        write(section, "LightsBroken",           offsets.LightsBroken);
        write(section, "LightsVisuallyBroken",   offsets.LightsVisuallyBroken);
        write(section, "HoverTransformRatioLerp",offsets.HoverTransformRatioLerp);
        write(section, "GearRatios",             offsets.GearRatios);
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
{}

bool VehicleData::CanRead (uint32_t offset, size_t) const { return m_isValid && offset != 0; }
bool VehicleData::CanWrite(uint32_t offset, size_t) const { return m_isValid && offset != 0; }
bool VehicleData::IsValid() const { return m_isValid; }

bool VehicleData::HasPlausibleLayout(int maxGear) const {
    if (!IsValid() || maxGear < 1 || maxGear > 32) return false;

    const uint8_t gear     = GetGear();
    const uint8_t nextGear = GetNextGear();
    const float   clutch   = GetClutch();
    const float   rpm      = GetRPM();

    // 0xFF == invalid / neutral on some vehicles
    const bool gearOk     = gear     <= static_cast<uint8_t>(maxGear + 1) || gear     == 0xFF;
    const bool nextGearOk = nextGear <= static_cast<uint8_t>(maxGear + 1) || nextGear == 0xFF;

    return gearOk && nextGearOk &&
           std::isfinite(clutch) && clutch >= -0.25f && clutch <= 2.0f &&
           std::isfinite(rpm)    && rpm    >= -0.25f && rpm    <= 2.5f;
}

// ── Getters ──────────────────────────────────────────────────────────────────
uint8_t VehicleData::GetGear()               const { return m_vehicle.GetGear(); }
uint8_t VehicleData::GetNextGear()           const { return m_vehicle.GetNextGear(); }
uint8_t VehicleData::GetTopGear()            const { return m_vehicle.GetTopGear(); }
float   VehicleData::GetClutch()             const { return m_vehicle.GetClutch(); }
float   VehicleData::GetRPM()               const { return m_vehicle.GetRPM(); }
float   VehicleData::GetFuelLevel()          const { return m_vehicle.GetFuelLevel(); }
uint8_t VehicleData::GetLightsBroken()       const { return m_vehicle.GetLightsBroken(); }
uint8_t VehicleData::GetLightsVisuallyBroken() const { return m_vehicle.GetLightsVisuallyBroken(); }
float   VehicleData::GetHoverTransformRatioLerp() const { return m_vehicle.GetHoverTransformRatioLerp(); }

float VehicleData::GetDriveForce() const {
    if (!m_isValid) return 0.0f;
    auto h = m_vehicle.GetHandlingData();
    return h.IsValid() ? h.GetDriveForce() : 0.0f;
}

float VehicleData::GetGearRatio(uint8_t gear) const {
    if (!m_isValid) return 0.0f;
    auto h = m_vehicle.GetHandlingData();
    return h.IsValid() ? h.GetGearRatio(gear) : 0.0f;
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
bool VehicleData::SetClutch(float clutch) {
    if (!m_isValid || !std::isfinite(clutch)) return false;
    m_vehicle.SetClutch(clutch);
    return true;
}
bool VehicleData::SetRPM(float rpm) {
    if (!m_isValid || !std::isfinite(rpm)) return false;
    m_vehicle.SetRPM(rpm);
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