#include "VehicleData.h"

#include "../../sdk/inc/main.h"
#include "../Memory/AOBScanner.h"

#define NOMINMAX
#include <Windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
#include <winver.h>

#pragma comment(lib, "Version.lib")

VehicleOffsets VehicleData::resolvedOffsets{};
VehicleOffsetSource VehicleData::offsetSource =
    VehicleOffsetSource::Uninitialized;
bool VehicleData::initialized = false;
const char *VehicleData::lastFailureReason = "not initialized";

CalibrationState VehicleData::calibState = CalibrationState::None;
std::vector<uint32_t> VehicleData::candidateOffsets;

namespace {

constexpr char kGearPattern[] = "A8 02 0F 84 ? ? ? ? 0F B7 86";
constexpr ptrdiff_t kNextGearDisplacement = 11;
constexpr ptrdiff_t kCurrentGearDisplacement = 18;

constexpr char kRpmPattern[] = "F6 83 ? ? ? ? 07 75 ? 44 0F";
constexpr ptrdiff_t kRpmDisplacement = -42;

bool IsWritableProtection(DWORD protection) {
  if ((protection & PAGE_GUARD) != 0 || (protection & PAGE_NOACCESS) != 0) {
    return false;
  }

  const DWORD baseProtection = protection & 0xFF;
  return baseProtection == PAGE_READWRITE || baseProtection == PAGE_WRITECOPY ||
         baseProtection == PAGE_EXECUTE_READWRITE ||
         baseProtection == PAGE_EXECUTE_WRITECOPY;
}

bool IsWritableAddress(uintptr_t address, size_t size) {
  if (address == 0 || size == 0)
    return false;

  MEMORY_BASIC_INFORMATION info{};
  if (VirtualQuery(reinterpret_cast<const void *>(address), &info,
                   sizeof(info)) == 0) {
    return false;
  }

  if (info.State != MEM_COMMIT || !IsWritableProtection(info.Protect)) {
    return false;
  }

  const uintptr_t start = reinterpret_cast<uintptr_t>(info.BaseAddress);
  const uintptr_t end = start + info.RegionSize;
  if (address < start || address > end)
    return false;
  return size <= end - address;
}

bool TryReadU32(uintptr_t address, uint32_t &value) {
  if (!AOBScanner::IsReadable(address, sizeof(value)))
    return false;
  std::memcpy(&value, reinterpret_cast<const void *>(address), sizeof(value));
  return true;
}

bool BuildIniPath(HMODULE pluginModule, char (&path)[MAX_PATH]) {
  DWORD length = 0;
  if (pluginModule != nullptr) {
    length = GetModuleFileNameA(pluginModule, path, MAX_PATH);
  }

  if (length == 0 || length >= MAX_PATH) {
    length = GetCurrentDirectoryA(MAX_PATH, path);
    if (length == 0 || length >= MAX_PATH)
      return false;
  } else {
    char *slash = std::strrchr(path, '\\');
    if (slash == nullptr)
      slash = std::strrchr(path, '/');
    if (slash == nullptr)
      return false;
    *slash = '\0';
  }

  return strcat_s(path, "\\manual-trans.ini") == 0;
}

uint32_t ReadHexOffset(const char *iniPath, const char *section,
                       const char *key) {
  char buffer[32]{};
  const DWORD count = GetPrivateProfileStringA(section, key, "", buffer,
                                               sizeof(buffer), iniPath);
  if (count == 0)
    return 0;

  char *end = nullptr;
  const unsigned long value = std::strtoul(buffer, &end, 0);
  if (end == buffer || *end != '\0' ||
      value > (std::numeric_limits<uint32_t>::max)()) {
    return 0;
  }

  return static_cast<uint32_t>(value);
}

// Reads the running module's PE version resource ("A.B.C.D"). Used to
// build/name a per-build ini offsets section, and to show the detected
// game build in the load/fail notification so you know which
// [Offsets.<version>] section to write.
bool GetModuleFileVersion(HMODULE module, std::string &out) {
  char path[MAX_PATH]{};
  if (GetModuleFileNameA(module, path, MAX_PATH) == 0)
    return false;

  DWORD handle = 0;
  const DWORD size = GetFileVersionInfoSizeA(path, &handle);
  if (size == 0)
    return false;

  std::vector<char> buffer(size);
  if (!GetFileVersionInfoA(path, handle, size, buffer.data()))
    return false;

  VS_FIXEDFILEINFO *info = nullptr;
  UINT infoSize = 0;
  if (!VerQueryValueA(buffer.data(), "\\", reinterpret_cast<void **>(&info),
                      &infoSize) ||
      info == nullptr) {
    return false;
  }

  char text[64]{};
  sprintf_s(text, "%u.%u.%u.%u", HIWORD(info->dwFileVersionMS),
            LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS),
            LOWORD(info->dwFileVersionLS));
  out = text;
  return true;
}

} // namespace

bool VehicleOffsets::IsComplete() const {
  return Gear != 0 && NextGear != 0 && Clutch != 0 && RPM != 0;
}

std::string VehicleData::GetGameBuildVersion() {
  std::string version;
  if (!GetModuleFileVersion(GetModuleHandleW(nullptr), version))
    return "";
  return version;
}

bool VehicleData::AreOffsetsSane(const VehicleOffsets &value) {
  if (!value.IsComplete())
    return false;

  constexpr uint32_t kMinimumOffset = 0x100;
  constexpr uint32_t kMaximumOffset = 0x8000;
  const uint32_t fields[] = {value.Gear, value.NextGear, value.Clutch,
                             value.RPM};

  for (const uint32_t field : fields) {
    if (field < kMinimumOffset || field >= kMaximumOffset)
      return false;
  }

  const uint32_t gearDistance = value.Gear > value.NextGear
                                    ? value.Gear - value.NextGear
                                    : value.NextGear - value.Gear;

  if (value.Gear == value.NextGear || gearDistance > 0x20)
    return false;
  const uint32_t driveDistance = value.Clutch > value.RPM
                                     ? value.Clutch - value.RPM
                                     : value.RPM - value.Clutch;
  if (value.Clutch == value.RPM || driveDistance > 0x40)
    return false;
  if ((value.RPM & 0x3) != 0 || (value.Clutch & 0x3) != 0)
    return false;

  return true;
}

bool VehicleData::ResolveOffsetsByPattern(VehicleOffsets &result) {
  // Each pattern is checked individually (rather than collapsed into one
  // FindUnique() call) so a failed scan can tell you *which* signature is
  // stale after a game update, instead of just "offsets unresolved".
  const auto gearMatches = AOBScanner::FindAll(kGearPattern, 2);
  if (gearMatches.empty()) {
    lastFailureReason = "gear pattern not found (game build outdated)";
    return false;
  }
  if (gearMatches.size() > 1) {
    lastFailureReason = "gear pattern matched multiple times (ambiguous)";
    return false;
  }

  const auto rpmMatches = AOBScanner::FindAll(kRpmPattern, 2);
  if (rpmMatches.empty()) {
    lastFailureReason = "rpm pattern not found (game build outdated)";
    return false;
  }
  if (rpmMatches.size() > 1) {
    lastFailureReason = "rpm pattern matched multiple times (ambiguous)";
    return false;
  }

  const uintptr_t gearMatch = gearMatches.front();
  const uintptr_t rpmMatch = rpmMatches.front();
  if (rpmMatch < static_cast<uintptr_t>(-kRpmDisplacement)) {
    lastFailureReason = "rpm match too close to module base";
    return false;
  }

  VehicleOffsets candidate{};
  if (!TryReadU32(gearMatch + kCurrentGearDisplacement, candidate.Gear) ||
      !TryReadU32(gearMatch + kNextGearDisplacement, candidate.NextGear) ||
      !TryReadU32(rpmMatch - static_cast<uintptr_t>(-kRpmDisplacement),
                  candidate.RPM)) {
    lastFailureReason = "could not read pattern displacement bytes";
    return false;
  }

  candidate.Clutch = candidate.RPM + 12;
  if (!AreOffsetsSane(candidate)) {
    lastFailureReason =
        "pattern offsets failed sanity check (game build outdated)";
    return false;
  }

  result = candidate;
  return true;
}

bool VehicleData::LoadOffsetsFromIni(HMODULE pluginModule,
                                     VehicleOffsets &result) {
  char iniPath[MAX_PATH]{};
  if (!BuildIniPath(pluginModule, iniPath))
    return false;

  // Try a build-specific section first, e.g. [Offsets.1.0.1013.20]. This
  // lets you keep verified offsets for several game builds in the same
  // ini without one overwriting the other - much safer than a single
  // generic section once you're chasing more than one build.
  const std::string buildVersion = GetGameBuildVersion();
  if (!buildVersion.empty()) {
    const std::string versionedSection = "Offsets." + buildVersion;
    VehicleOffsets versioned{};
    versioned.Gear = ReadHexOffset(iniPath, versionedSection.c_str(), "Gear");
    versioned.NextGear =
        ReadHexOffset(iniPath, versionedSection.c_str(), "NextGear");
    versioned.Clutch =
        ReadHexOffset(iniPath, versionedSection.c_str(), "Clutch");
    versioned.RPM = ReadHexOffset(iniPath, versionedSection.c_str(), "RPM");

    if (AreOffsetsSane(versioned)) {
      result = versioned;
      return true;
    }
  }

  // Fall back to the generic [Offsets] section.
  VehicleOffsets candidate{};
  candidate.Gear = ReadHexOffset(iniPath, "Offsets", "Gear");
  candidate.NextGear = ReadHexOffset(iniPath, "Offsets", "NextGear");
  candidate.Clutch = ReadHexOffset(iniPath, "Offsets", "Clutch");
  candidate.RPM = ReadHexOffset(iniPath, "Offsets", "RPM");

  if (!AreOffsetsSane(candidate))
    return false;

  result = candidate;
  return true;
}

void VehicleData::SaveOffsetsToIni(HMODULE pluginModule, const VehicleOffsets &offsets) {
  char iniPath[MAX_PATH]{};
  if (!BuildIniPath(pluginModule, iniPath))
    return;

  const std::string buildVersion = GetGameBuildVersion();
  std::string sectionName = "Offsets";
  if (!buildVersion.empty()) {
    sectionName += "." + buildVersion;
  }

  char buffer[32]{};
  sprintf_s(buffer, "0x%X", offsets.Gear);
  WritePrivateProfileStringA(sectionName.c_str(), "Gear", buffer, iniPath);

  sprintf_s(buffer, "0x%X", offsets.NextGear);
  WritePrivateProfileStringA(sectionName.c_str(), "NextGear", buffer, iniPath);

  sprintf_s(buffer, "0x%X", offsets.Clutch);
  WritePrivateProfileStringA(sectionName.c_str(), "Clutch", buffer, iniPath);

  sprintf_s(buffer, "0x%X", offsets.RPM);
  WritePrivateProfileStringA(sectionName.c_str(), "RPM", buffer, iniPath);
  
  WritePrivateProfileStringA("Memory", "AllowIniFallback", "1", iniPath);
}

bool VehicleData::Initialize(HMODULE pluginModule) {
  if (initialized)
    return true;

  VehicleOffsets candidate{};
  if (ResolveOffsetsByPattern(candidate)) {
    resolvedOffsets = candidate;
    offsetSource = VehicleOffsetSource::PatternScan;
    initialized = true;
    return true;
  }

  // lastFailureReason was already set inside ResolveOffsetsByPattern().
  // It gets overwritten below only if the ini fallback path also fails,
  // since that's the more actionable message for the user to see.
  char iniPath[MAX_PATH]{};
  const bool hasIniPath = BuildIniPath(pluginModule, iniPath);
  const bool allowFallback =
      hasIniPath &&
      GetPrivateProfileIntA("Memory", "AllowIniFallback", 0, iniPath) != 0;

  if (!allowFallback) {
    lastFailureReason =
        hasIniPath
            ? "AOB failed; INI fallback disabled (set AllowIniFallback=1)"
            : "AOB failed; INI path unresolved";
    if (allowFallback && LoadOffsetsFromIni(pluginModule, candidate)) {
      resolvedOffsets = candidate;
      offsetSource = VehicleOffsetSource::IniFallback;
      initialized = true;
      return true;
    }
  }

  resolvedOffsets = {};
  offsetSource = VehicleOffsetSource::Uninitialized;
  initialized = false;
  
  if (calibState == CalibrationState::None) {
      calibState = CalibrationState::WaitingForEngineOff;
  }
  return false;
}

void VehicleData::ResetCalibration() {
    calibState = CalibrationState::WaitingForEngineOff;
    candidateOffsets.clear();
    resolvedOffsets = {};
    initialized = false;
    offsetSource = VehicleOffsetSource::Uninitialized;
}

CalibrationState VehicleData::GetCalibrationState() {
    return calibState;
}

void VehicleData::UpdateCalibration(HMODULE pluginModule, int vehicleHandle, bool isEngineOn, bool isRevving) {
    if (initialized || calibState == CalibrationState::None || calibState == CalibrationState::Done || calibState == CalibrationState::Failed) return;

    BYTE* base = getScriptHandleBaseAddress(vehicleHandle);
    if (!base) return;

    if (calibState == CalibrationState::WaitingForEngineOff) {
        if (!isEngineOn) {
            calibState = CalibrationState::ScanningEngineOff;
        }
    } else if (calibState == CalibrationState::ScanningEngineOff) {
        candidateOffsets.clear();
        for (uint32_t offset = 0x700; offset < 0xA00; offset += 4) {
            if (AOBScanner::IsReadable(reinterpret_cast<uintptr_t>(base) + offset, 4)) {
                float val = *reinterpret_cast<float*>(base + offset);
                if (val == 0.0f) {
                    candidateOffsets.push_back(offset);
                }
            }
        }
        calibState = CalibrationState::WaitingForEngineOn;
    } else if (calibState == CalibrationState::WaitingForEngineOn) {
        if (isEngineOn) {
            calibState = CalibrationState::ScanningEngineOn;
        }
    } else if (calibState == CalibrationState::ScanningEngineOn) {
        std::vector<uint32_t> nextCandidates;
        for (uint32_t offset : candidateOffsets) {
            float val = *reinterpret_cast<float*>(base + offset);
            if (val > 0.1f && val < 0.3f) {
                nextCandidates.push_back(offset);
            }
        }
        candidateOffsets = nextCandidates;
        calibState = CalibrationState::WaitingForRev;
    } else if (calibState == CalibrationState::WaitingForRev) {
        if (isRevving) {
            calibState = CalibrationState::ScanningRev;
        }
    } else if (calibState == CalibrationState::ScanningRev) {
        std::vector<uint32_t> nextCandidates;
        for (uint32_t offset : candidateOffsets) {
            float val = *reinterpret_cast<float*>(base + offset);
            if (val > 0.4f && val <= 1.2f) { // RPM increases
                nextCandidates.push_back(offset);
            }
        }
        candidateOffsets = nextCandidates;
        
        if (candidateOffsets.size() == 1) {
            resolvedOffsets.RPM = candidateOffsets[0];
            resolvedOffsets.Clutch = resolvedOffsets.RPM + 12; // Typical GTA V memory layout
            resolvedOffsets.Gear = resolvedOffsets.RPM - 36;
            resolvedOffsets.NextGear = resolvedOffsets.Gear - 2;

            if (AreOffsetsSane(resolvedOffsets)) {
                SaveOffsetsToIni(pluginModule, resolvedOffsets);
                offsetSource = VehicleOffsetSource::Calibration;
                initialized = true;
                calibState = CalibrationState::Done;
            } else {
                calibState = CalibrationState::Failed;
                lastFailureReason = "Calibration found invalid offset layout";
            }
        } else if (candidateOffsets.empty()) {
            calibState = CalibrationState::Failed;
            lastFailureReason = "Calibration failed to find unique RPM offset";
        }
    }
}

bool VehicleData::IsInitialized() { return initialized; }

VehicleOffsetSource VehicleData::GetOffsetSource() { return offsetSource; }

const char *VehicleData::GetOffsetSourceName() {
  switch (offsetSource) {
  case VehicleOffsetSource::PatternScan:
    return "AOB";
  case VehicleOffsetSource::IniFallback:
    return "INI fallback";
  default:
    return "unresolved";
  }
}

const VehicleOffsets &VehicleData::GetResolvedOffsets() {
  return resolvedOffsets;
}

const char *VehicleData::GetLastFailureReason() { return lastFailureReason; }

VehicleData::VehicleData(int vehicleHandle) {
  if (!initialized)
    return;
  baseAddress = getScriptHandleBaseAddress(vehicleHandle);
}

bool VehicleData::CanRead(uint32_t offset, size_t size) const {
  if (!initialized || baseAddress == nullptr || offset == 0)
    return false;
  return AOBScanner::IsReadable(
      reinterpret_cast<uintptr_t>(baseAddress) + offset, size);
}

bool VehicleData::CanWrite(uint32_t offset, size_t size) const {
  if (!initialized || baseAddress == nullptr || offset == 0)
    return false;
  return IsWritableAddress(reinterpret_cast<uintptr_t>(baseAddress) + offset,
                           size);
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
  if (!CanRead(resolvedOffsets.Gear, sizeof(uint8_t)))
    return 0;
  return *reinterpret_cast<const uint8_t *>(baseAddress + resolvedOffsets.Gear);
}

uint8_t VehicleData::GetNextGear() const {
  if (!CanRead(resolvedOffsets.NextGear, sizeof(uint8_t)))
    return 0;
  return *reinterpret_cast<const uint8_t *>(baseAddress +
                                            resolvedOffsets.NextGear);
}

float VehicleData::GetClutch() const {
  if (!CanRead(resolvedOffsets.Clutch, sizeof(float)))
    return 0.0f;
  return *reinterpret_cast<const float *>(baseAddress + resolvedOffsets.Clutch);
}

float VehicleData::GetRPM() const {
  if (!CanRead(resolvedOffsets.RPM, sizeof(float)))
    return 0.0f;
  return *reinterpret_cast<const float *>(baseAddress + resolvedOffsets.RPM);
}

bool VehicleData::HasPlausibleLayout(int maxGear) const {
  if (!IsValid() || maxGear < 1 || maxGear > 32)
    return false;

  const uint8_t gear = GetGear();
  const uint8_t nextGear = GetNextGear();
  const float clutch = GetClutch();
  const float rpm = GetRPM();

  const bool gearPlausible =
      gear <= static_cast<uint8_t>(maxGear + 1) || gear == 0xFF;
  const bool nextGearPlausible =
      nextGear <= static_cast<uint8_t>(maxGear + 1) || nextGear == 0xFF;

  return gearPlausible && nextGearPlausible && std::isfinite(clutch) &&
         clutch >= -0.25f && clutch <= 2.0f && std::isfinite(rpm) &&
         rpm >= -0.25f && rpm <= 2.5f;
}

bool VehicleData::SetGear(uint8_t gear) {
  if (!CanWrite(resolvedOffsets.Gear, sizeof(gear)))
    return false;
  *reinterpret_cast<uint8_t *>(baseAddress + resolvedOffsets.Gear) = gear;
  return true;
}

bool VehicleData::SetNextGear(uint8_t gear) {
  if (!CanWrite(resolvedOffsets.NextGear, sizeof(gear)))
    return false;
  *reinterpret_cast<uint8_t *>(baseAddress + resolvedOffsets.NextGear) = gear;
  return true;
}

bool VehicleData::SetClutch(float clutch) {
  if (!std::isfinite(clutch) ||
      !CanWrite(resolvedOffsets.Clutch, sizeof(clutch))) {
    return false;
  }
  *reinterpret_cast<float *>(baseAddress + resolvedOffsets.Clutch) = clutch;
  return true;
}

bool VehicleData::SetRPM(float rpm) {
  if (!std::isfinite(rpm) || !CanWrite(resolvedOffsets.RPM, sizeof(rpm))) {
    return false;
  }
  *reinterpret_cast<float *>(baseAddress + resolvedOffsets.RPM) = rpm;
  return true;
}