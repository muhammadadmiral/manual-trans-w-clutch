#include "VehicleData.h"

#include "../../sdk/inc/main.h"
#include "../Memory/AOBScanner.h"
#include "../Core/ModLogger.h"

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
std::string VehicleData::lastFailureReason = "not initialized";

CalibrationState VehicleData::calibState = CalibrationState::None;
std::vector<uint32_t> VehicleData::candidateOffsets;
std::vector<float> VehicleData::candidateIdleValues;
uint64_t VehicleData::phaseEnterTick = 0;

namespace {

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
                   sizeof(info)) == 0)
    return false;
  if (info.State != MEM_COMMIT || !IsWritableProtection(info.Protect))
    return false;

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

bool GetModuleFileVersion(HMODULE module, std::string &out) {
  char path[MAX_PATH]{};
  if (GetModuleFileNameA(module, path, MAX_PATH) == 0)
    return false;

  DWORD handle = 0;
  const DWORD size = GetFileVersionInfoSizeA(path, &handle);
  if (size == 0)
    return false;

  std::vector<char> buffer(size);
  if (!GetFileVersionInfoA(path, 0, size, buffer.data()))
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

std::string VehicleData::GetGameBuildVersion() {
  std::string version;
  if (!GetModuleFileVersion(GetModuleHandleW(nullptr), version))
    return "";
  return version;
}

bool VehicleData::AreOffsetsSane(const VehicleOffsets &value) {
  if (!value.IsCompleteCore())
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
  std::string failureReason;
  if (!OffsetResolver::ScanPatterns(result, failureReason)) {
    lastFailureReason = failureReason;
    return false;
  }

  if (!AreOffsetsSane(result)) {
    lastFailureReason =
        "pattern offsets failed sanity check (game build outdated)";
    return false;
  }
  return true;
}

bool VehicleData::LoadOffsetsFromIni(HMODULE pluginModule,
                                     VehicleOffsets &result) {
  char iniPath[MAX_PATH]{};
  if (!BuildIniPath(pluginModule, iniPath))
    return false;

  if (GetPrivateProfileIntA("Memory", "AllowIniFallback", 1, iniPath) == 0) {
    return false;
  }

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
    versioned.TopGear =
        ReadHexOffset(iniPath, versionedSection.c_str(), "TopGear");
    versioned.DriveForce =
        ReadHexOffset(iniPath, versionedSection.c_str(), "DriveForce");
    versioned.FuelLevel =
        ReadHexOffset(iniPath, versionedSection.c_str(), "FuelLevel");
    versioned.LightsBroken =
        ReadHexOffset(iniPath, versionedSection.c_str(), "LightsBroken");
    versioned.LightsVisuallyBroken = ReadHexOffset(
        iniPath, versionedSection.c_str(), "LightsVisuallyBroken");
    versioned.HoverTransformRatioLerp = ReadHexOffset(
        iniPath, versionedSection.c_str(), "HoverTransformRatioLerp");
    versioned.GearRatios =
        ReadHexOffset(iniPath, versionedSection.c_str(), "GearRatios");

    if (AreOffsetsSane(versioned)) {
      result = versioned;
      return true;
    }
  }

  VehicleOffsets candidate{};
  candidate.Gear = ReadHexOffset(iniPath, "Offsets", "Gear");
  candidate.NextGear = ReadHexOffset(iniPath, "Offsets", "NextGear");
  candidate.Clutch = ReadHexOffset(iniPath, "Offsets", "Clutch");
  candidate.RPM = ReadHexOffset(iniPath, "Offsets", "RPM");
  candidate.TopGear = ReadHexOffset(iniPath, "Offsets", "TopGear");
  candidate.DriveForce = ReadHexOffset(iniPath, "Offsets", "DriveForce");
  candidate.FuelLevel = ReadHexOffset(iniPath, "Offsets", "FuelLevel");
  candidate.LightsBroken = ReadHexOffset(iniPath, "Offsets", "LightsBroken");
  candidate.LightsVisuallyBroken =
      ReadHexOffset(iniPath, "Offsets", "LightsVisuallyBroken");
  candidate.HoverTransformRatioLerp =
      ReadHexOffset(iniPath, "Offsets", "HoverTransformRatioLerp");
  candidate.GearRatios = ReadHexOffset(iniPath, "Offsets", "GearRatios");

  if (!AreOffsetsSane(candidate))
    return false;

  result = candidate;
  return true;
}

void VehicleData::SaveOffsetsToIni(HMODULE pluginModule,
                                   const VehicleOffsets &offsets) {
  char iniPath[MAX_PATH]{};
  if (!BuildIniPath(pluginModule, iniPath)) {
    LOG_ERROR(MEM, "SaveOffsetsToIni: BuildIniPath failed — offsets not saved.");
    return;
  }

  char buffer[32]{};

  // Save generic
  sprintf_s(buffer, "0x%X", offsets.Gear);
  WritePrivateProfileStringA("Offsets", "Gear", buffer, iniPath);
  sprintf_s(buffer, "0x%X", offsets.NextGear);
  WritePrivateProfileStringA("Offsets", "NextGear", buffer, iniPath);
  sprintf_s(buffer, "0x%X", offsets.Clutch);
  WritePrivateProfileStringA("Offsets", "Clutch", buffer, iniPath);
  sprintf_s(buffer, "0x%X", offsets.RPM);
  WritePrivateProfileStringA("Offsets", "RPM", buffer, iniPath);
  sprintf_s(buffer, "0x%X", offsets.TopGear);
  WritePrivateProfileStringA("Offsets", "TopGear", buffer, iniPath);
  sprintf_s(buffer, "0x%X", offsets.DriveForce);
  WritePrivateProfileStringA("Offsets", "DriveForce", buffer, iniPath);
  sprintf_s(buffer, "0x%X", offsets.FuelLevel);
  WritePrivateProfileStringA("Offsets", "FuelLevel", buffer, iniPath);
  sprintf_s(buffer, "0x%X", offsets.LightsBroken);
  WritePrivateProfileStringA("Offsets", "LightsBroken", buffer, iniPath);
  sprintf_s(buffer, "0x%X", offsets.LightsVisuallyBroken);
  WritePrivateProfileStringA("Offsets", "LightsVisuallyBroken", buffer, iniPath);
  sprintf_s(buffer, "0x%X", offsets.HoverTransformRatioLerp);
  WritePrivateProfileStringA("Offsets", "HoverTransformRatioLerp", buffer, iniPath);
  sprintf_s(buffer, "0x%X", offsets.GearRatios);
  WritePrivateProfileStringA("Offsets", "GearRatios", buffer, iniPath);

  // Save versioned
  const std::string buildVersion = GetGameBuildVersion();
  if (!buildVersion.empty()) {
    std::string versionedSection = "Offsets." + buildVersion;
    sprintf_s(buffer, "0x%X", offsets.Gear);
    WritePrivateProfileStringA(versionedSection.c_str(), "Gear", buffer, iniPath);
    sprintf_s(buffer, "0x%X", offsets.NextGear);
    WritePrivateProfileStringA(versionedSection.c_str(), "NextGear", buffer, iniPath);
    sprintf_s(buffer, "0x%X", offsets.Clutch);
    WritePrivateProfileStringA(versionedSection.c_str(), "Clutch", buffer, iniPath);
    sprintf_s(buffer, "0x%X", offsets.RPM);
    WritePrivateProfileStringA(versionedSection.c_str(), "RPM", buffer, iniPath);
    sprintf_s(buffer, "0x%X", offsets.TopGear);
    WritePrivateProfileStringA(versionedSection.c_str(), "TopGear", buffer, iniPath);
    sprintf_s(buffer, "0x%X", offsets.DriveForce);
    WritePrivateProfileStringA(versionedSection.c_str(), "DriveForce", buffer, iniPath);
    sprintf_s(buffer, "0x%X", offsets.FuelLevel);
    WritePrivateProfileStringA(versionedSection.c_str(), "FuelLevel", buffer, iniPath);
    sprintf_s(buffer, "0x%X", offsets.LightsBroken);
    WritePrivateProfileStringA(versionedSection.c_str(), "LightsBroken", buffer, iniPath);
    sprintf_s(buffer, "0x%X", offsets.LightsVisuallyBroken);
    WritePrivateProfileStringA(versionedSection.c_str(), "LightsVisuallyBroken", buffer, iniPath);
    sprintf_s(buffer, "0x%X", offsets.HoverTransformRatioLerp);
    WritePrivateProfileStringA(versionedSection.c_str(), "HoverTransformRatioLerp", buffer, iniPath);
    sprintf_s(buffer, "0x%X", offsets.GearRatios);
    WritePrivateProfileStringA(versionedSection.c_str(), "GearRatios", buffer, iniPath);
  }

  WritePrivateProfileStringA("Memory", "AllowIniFallback", "1", iniPath);
  LOG_INFO(MEM, "Offsets saved to INI: G=0x%X N=0x%X RPM=0x%X CLT=0x%X [%s]",
           offsets.Gear, offsets.NextGear, offsets.RPM, offsets.Clutch,
           buildVersion.empty() ? "generic" : buildVersion.c_str());
}

bool VehicleData::Initialize(HMODULE pluginModule) {
  if (initialized)
    return true;

  LOG_INFO(INIT, "VehicleData::Initialize — attempting AOB pattern scan...");

  VehicleOffsets candidate{};
  if (ResolveOffsetsByPattern(candidate)) {
    resolvedOffsets = candidate;
    offsetSource = VehicleOffsetSource::PatternScan;
    initialized = true;
    LOG_INFO(INIT, "AOB scan succeeded — G=0x%X N=0x%X RPM=0x%X CLT=0x%X",
             candidate.Gear, candidate.NextGear, candidate.RPM, candidate.Clutch);
    SaveOffsetsToIni(pluginModule, candidate);
    return true;
  }

  LOG_WARN(INIT, "AOB scan failed (%s). Trying INI fallback...",
           lastFailureReason.c_str());

  if (LoadOffsetsFromIni(pluginModule, candidate)) {
    resolvedOffsets = candidate;
    offsetSource = VehicleOffsetSource::IniFallback;
    initialized = true;
    LOG_INFO(INIT, "INI fallback loaded — G=0x%X N=0x%X RPM=0x%X CLT=0x%X",
             candidate.Gear, candidate.NextGear, candidate.RPM, candidate.Clutch);
    return true;
  }

  LOG_WARN(INIT, "INI fallback unavailable. Starting interactive calibration.");
  offsetSource = VehicleOffsetSource::Calibration;
  initialized = false;

  if (calibState == CalibrationState::None) {
    calibState = CalibrationState::WaitingForEngineOff;
    LOG_INFO(CALIB, "Calibration state machine initialized: WaitingForEngineOff");
  }

  return true;
}

void VehicleData::ResetCalibration() {
  LOG_INFO(CALIB, "ResetCalibration called — wiping all resolved offsets and candidate lists.");
  initialized = false;
  calibState = CalibrationState::WaitingForEngineOff;
  offsetSource = VehicleOffsetSource::Uninitialized;
  resolvedOffsets = {};
  candidateOffsets.clear();
  candidateIdleValues.clear();
  phaseEnterTick = 0;
}

CalibrationState VehicleData::GetCalibrationState() { return calibState; }
size_t VehicleData::GetCalibrationCandidateCount() {
  return candidateOffsets.size();
}

void VehicleData::UpdateCalibration(HMODULE pluginModule, int vehicleHandle,
                                    bool isEngineOn, bool isRevving) {
  if (initialized || calibState == CalibrationState::None ||
      calibState == CalibrationState::Done ||
      calibState == CalibrationState::Failed)
    return;

  // We only run calibration against the native memory engine.
  GameMemory::CVehicle calibVehicle(
      reinterpret_cast<uintptr_t>(getScriptHandleBaseAddress(vehicleHandle)),
      &resolvedOffsets);

  if (!calibVehicle.IsValid()) {
    lastFailureReason = "invalid vehicle handle or pointer";
    return;
  }

  if (calibState == CalibrationState::WaitingForEngineOff) {
    if (!isEngineOn) {
      LOG_INFO(CALIB, "Engine turned off — entering ScanningEngineOff phase.");
      calibState = CalibrationState::ScanningEngineOff;
    }
  } else if (calibState == CalibrationState::ScanningEngineOff) {
    if (isEngineOn) {
      LOG_WARN(CALIB, "Engine came back on during ScanningEngineOff — restarting.");
      calibState = CalibrationState::WaitingForEngineOff;
      phaseEnterTick = 0;
      return;
    }
    const uint64_t now = GetTickCount64();
    if (phaseEnterTick == 0) {
      phaseEnterTick = now;
      LOG_DEBUG(CALIB, "RPM settle timer started (kEngineOffSettleMs=%llu ms).", kEngineOffSettleMs);
      return;
    }
    if (now - phaseEnterTick < kEngineOffSettleMs)
      return;

    phaseEnterTick = 0;
    candidateOffsets.clear();
    int scanned = 0;
    for (uint32_t offset = kCalibScanStart; offset < kCalibScanEnd; offset += 4) {
      if (AOBScanner::IsReadable(calibVehicle.GetAddress() + offset, 4)) {
        ++scanned;
        float val = *reinterpret_cast<float *>(calibVehicle.GetAddress() + offset);
        if (val > -0.01f && val < 0.1f)
          candidateOffsets.push_back(offset);
      }
    }
    LOG_INFO(CALIB,
             "Engine-off scan done: scanned=%d nearZero=%zu (window 0x%X-0x%X)",
             scanned, candidateOffsets.size(), kCalibScanStart, kCalibScanEnd);

    if (candidateOffsets.empty()) {
      calibState = CalibrationState::Failed;
      char msg[160]{};
      sprintf_s(msg,
                "0 candidates in 0x%X-0x%X with engine off. Field is outside "
                "this window - widen kCalibScanStart/End in VehicleData.h.",
                kCalibScanStart, kCalibScanEnd);
      lastFailureReason = msg;
      LOG_ERROR(CALIB, "Calibration FAILED (engine-off scan): %s", msg);
    } else {
      calibState = CalibrationState::WaitingForEngineOn;
    }
  } else if (calibState == CalibrationState::WaitingForEngineOn) {
    if (isEngineOn) {
      LOG_INFO(CALIB, "Engine turned on — entering ScanningEngineOn phase.");
      calibState = CalibrationState::ScanningEngineOn;
    }
  } else if (calibState == CalibrationState::ScanningEngineOn) {
    if (!isEngineOn) {
      LOG_WARN(CALIB, "Engine turned off during ScanningEngineOn — reverting.");
      calibState = CalibrationState::WaitingForEngineOn;
      phaseEnterTick = 0;
      return;
    }
    const uint64_t now = GetTickCount64();
    if (phaseEnterTick == 0) {
      phaseEnterTick = now;
      LOG_DEBUG(CALIB, "Idle settle timer started (kIdleSettleMs=%llu ms).", kIdleSettleMs);
      return;
    }
    if (now - phaseEnterTick < kIdleSettleMs)
      return;

    phaseEnterTick = 0;
    std::vector<uint32_t> nextCandidates;
    std::vector<float>    nextIdleValues;
    for (uint32_t offset : candidateOffsets) {
      float val = *reinterpret_cast<float *>(calibVehicle.GetAddress() + offset);
      // Idle candidate: must have moved off 0 by a real amount.
      // Upper bound 20000 supports both 0-1 normalized and raw-RPM builds.
      if (val > 0.005f && val < 20000.0f) {
        nextCandidates.push_back(offset);
        nextIdleValues.push_back(val);
        LOG_DEBUG(CALIB, "  Idle candidate offset=0x%X idleVal=%.5f", offset, val);
      }
    }
    LOG_INFO(CALIB,
             "Idle scan done: before=%zu after=%zu (surviving idle candidates)",
             candidateOffsets.size(), nextCandidates.size());

    if (!nextCandidates.empty()) {
      candidateOffsets    = nextCandidates;
      candidateIdleValues = nextIdleValues;
      calibState = CalibrationState::WaitingForRev;
    } else {
      calibState = CalibrationState::Failed;
      lastFailureReason =
          "None of the zero-candidates moved off 0.0 once the engine was "
          "idling. Either the engine wasn't actually idling yet when this "
          "scanned (try recalibrating and wait a beat before it proceeds), "
          "or none of the 0x600-0x1400 window candidates are RPM at all - "
          "consider widening kCalibScanEnd.";
      LOG_ERROR(CALIB, "Calibration FAILED (idle scan): %s", lastFailureReason.c_str());
    }
  } else if (calibState == CalibrationState::WaitingForRev) {
    if (isRevving) {
      LOG_INFO(CALIB, "Throttle detected (>0.5) — entering ScanningRev phase.");
      calibState = CalibrationState::ScanningRev;
    }
  } else if (calibState == CalibrationState::ScanningRev) {
    if (!isRevving) {
      LOG_DEBUG(CALIB, "Throttle released during ScanningRev — reverting to WaitingForRev.");
      calibState = CalibrationState::WaitingForRev;
      phaseEnterTick = 0;
      return;
    }
    const uint64_t now = GetTickCount64();
    if (phaseEnterTick == 0) {
      phaseEnterTick = now;
      LOG_DEBUG(CALIB, "Rev settle timer started (kRevSettleMs=%llu ms).", kRevSettleMs);
      return;
    }
    if (now - phaseEnterTick < kRevSettleMs)
      return;

    phaseEnterTick = 0;
    std::vector<uint32_t> nextCandidates;
    std::vector<float>    nextIdleValues;
    bool foundRev = false;

    for (size_t i = 0; i < candidateOffsets.size(); ++i) {
      const uint32_t offset  = candidateOffsets[i];
      const float    idleVal = candidateIdleValues[i];
      float val = *reinterpret_cast<float *>(calibVehicle.GetAddress() + offset);
      // Relative threshold: must rise 30% above its own idle and by at least 0.005.
      // Works regardless of whether RPM is 0-1 normalized or raw revs.
      if (val > idleVal * 1.3f && (val - idleVal) > 0.005f) {
        nextCandidates.push_back(offset);
        nextIdleValues.push_back(idleVal);
        foundRev = true;
        LOG_DEBUG(CALIB,
                  "  Rev candidate offset=0x%X idleVal=%.5f revVal=%.5f (rise=%.1f%%)",
                  offset, idleVal, val, (val / idleVal - 1.0f) * 100.0f);
      }
    }

    LOG_INFO(CALIB,
             "Rev scan done: before=%zu after=%zu foundRev=%d",
             candidateOffsets.size(), nextCandidates.size(),
             static_cast<int>(foundRev));

    if (foundRev) {
      candidateOffsets = nextCandidates;
      if (!candidateOffsets.empty()) {
        bool anyCandidateSucceeded = false;

        for (uint32_t rpmCandidate : candidateOffsets) {
          resolvedOffsets.RPM   = rpmCandidate;
          resolvedOffsets.Clutch = rpmCandidate + 12;

          bool     foundGearLayout    = false;
          uint32_t foundGearOffset    = 0, foundNextGearOffset = 0,
                   foundTopGearOffset = 0, foundRatiosOffset   = 0;

          const uint32_t rpmOff   = resolvedOffsets.RPM;
          const uint32_t searchLo = rpmOff > kGearSearchBefore ? rpmOff - kGearSearchBefore : 0;
          const uint32_t searchHi = rpmOff + kGearSearchAfter;

          const bool regionReadable =
              AOBScanner::IsReadable(calibVehicle.GetAddress() + searchLo,
                                     (searchHi - searchLo) + 64 + 16);

          LOG_DEBUG(CALIB,
                    "  Trying RPM candidate 0x%X CLT=0x%X searchLo=0x%X searchHi=0x%X readable=%d",
                    rpmCandidate, resolvedOffsets.Clutch,
                    searchLo, searchHi, static_cast<int>(regionReadable));

          if (regionReadable) {
            foundGearLayout = SearchGearLayout(
                calibVehicle, searchLo, searchHi,
                foundGearOffset, foundNextGearOffset,
                foundTopGearOffset, foundRatiosOffset);
          }

          if (foundGearLayout) {
            resolvedOffsets.Gear      = foundGearOffset;
            resolvedOffsets.NextGear  = foundNextGearOffset;
            resolvedOffsets.TopGear   = foundTopGearOffset;
            resolvedOffsets.GearRatios = foundRatiosOffset;

            const bool sane = AreOffsetsSane(resolvedOffsets);
            LOG_DEBUG(CALIB,
                      "  GearLayout found: G=0x%X N=0x%X TG=0x%X Ratios=0x%X sane=%d",
                      foundGearOffset, foundNextGearOffset,
                      foundTopGearOffset, foundRatiosOffset,
                      static_cast<int>(sane));

            if (sane) {
              anyCandidateSucceeded = true;
              break;
            }
          } else {
            LOG_DEBUG(CALIB, "  SearchGearLayout returned false for RPM=0x%X", rpmCandidate);
          }
        } // end candidate loop

        if (anyCandidateSucceeded) {
          calibState  = CalibrationState::Done;
          initialized = true;
          LOG_INFO(CALIB,
                   "Calibration COMPLETE — RPM=0x%X CLT=0x%X G=0x%X N=0x%X TG=0x%X",
                   resolvedOffsets.RPM, resolvedOffsets.Clutch,
                   resolvedOffsets.Gear, resolvedOffsets.NextGear, resolvedOffsets.TopGear);
          SaveOffsetsToIni(pluginModule, resolvedOffsets);
        } else {
          resolvedOffsets.Gear    = 0;
          resolvedOffsets.NextGear = 0;
          calibState = CalibrationState::Failed;
          lastFailureReason =
              "RPM/Clutch located OK, but Gear/NextGear can't be safely guessed "
              "around ANY of the RPM candidates. Memory layout is unrecognized.";
          LOG_ERROR(CALIB,
                    "Calibration FAILED (gear layout): tried %zu RPM candidates, none passed sanity.",
                    candidateOffsets.size());
        }
      } else {
        calibState = CalibrationState::Failed;
        lastFailureReason = "Calibration failed to isolate RPM offset";
        LOG_ERROR(CALIB, "Calibration FAILED: no RPM candidates survived rev scan.");
      }
    } else {
      calibState = CalibrationState::Failed;
      lastFailureReason =
          "Held revs for over a second but no idle-candidate rose above its "
          "own idle value by 40%+. Either it wasn't revving hard enough to "
          "register (try holding W harder/longer), or the idle-stage "
          "candidates weren't RPM at all - recalibrate from scratch.";
      LOG_ERROR(CALIB,
                "Calibration FAILED (rev check): %zu idle candidates, none rose >=30%% above idle.",
                candidateOffsets.size());
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
  case VehicleOffsetSource::Calibration:
    return "Calibration (unverified - double check gears/lights!)";
  default:
    return "unresolved";
  }
}

const VehicleOffsets &VehicleData::GetResolvedOffsets() {
  return resolvedOffsets;
}

// =============================================================================
// Helper implementations
// =============================================================================

bool VehicleData::SearchGearLayout(const GameMemory::CVehicle &calibVehicle,
                                   uint32_t searchLo, uint32_t searchHi,
                                   uint32_t &outGearOffset,
                                   uint32_t &outNextGearOffset,
                                   uint32_t &outTopGearOffset,
                                   uint32_t &outRatiosOffset) {
  // ── Design notes ────────────────────────────────────────────────────────────
  // We step by 2 (not 8) because GearRatios pointer alignment isn't guaranteed
  // across all GTA V builds / struct packing variants.
  //
  // FIX vs original: topGear range widened from [4,9] → [1,16] to support:
  //   - electric vehicles / motorbikes (often 1-3 forward gears)
  //   - trucks / modded cars with 10+ gears
  //
  // FIX vs original: gear ratio plausibility now only requires the first
  // TWO ratios (reverse + 1st gear) to be finite & non-zero. Vehicles with
  // fewer gears leave higher slots as 0.0f, which was previously causing the
  // layout to be rejected even when it was correct.
  // ────────────────────────────────────────────────────────────────────────────
  __try {
    for (uint32_t ptrOff = searchLo; ptrOff <= searchHi; ptrOff += 2) {
      const uintptr_t ptr =
          *reinterpret_cast<uintptr_t *>(calibVehicle.GetAddress() + ptrOff);
      if (ptr < 0x10000000 || !AOBScanner::IsReadable(ptr, 16))
        continue;

      const float *ratios = reinterpret_cast<const float *>(ptr);

      // Only validate the first 2 gear ratios (Reverse & 1st gear).
      // All vehicles have at least those two; higher entries may be 0.0f
      // for vehicles with fewer gears.
      bool plausible = true;
      for (int g = 0; g < 2; ++g) {
        float r = ratios[g];
        if (!std::isfinite(r) || r < -20.0f || r > 20.0f ||
            (r > -0.05f && r < 0.05f)) {
          plausible = false;
          break;
        }
      }
      if (!plausible)
        continue;

      if (ptrOff < 8)
        continue;

      const uint32_t baseGearOffset = ptrOff - 8;
      const uint8_t nextGearVal = *reinterpret_cast<uint8_t *>(
          calibVehicle.GetAddress() + baseGearOffset);
      const uint8_t gearVal = *reinterpret_cast<uint8_t *>(
          calibVehicle.GetAddress() + baseGearOffset + 2);
      const uint8_t topGearVal = *reinterpret_cast<uint8_t *>(
          calibVehicle.GetAddress() + baseGearOffset + 6);

      // Current gear and next gear must be sensible (0 or 1 in idle/neutral).
      // Top gear widened to [1, 16] to support all vehicle types.
      if ((gearVal == 0 || gearVal == 1) &&
          (nextGearVal == 0 || nextGearVal == 1 || nextGearVal == 2) &&
          (topGearVal >= 1 && topGearVal <= 16)) {

        LOG_DEBUG(CALIB,
                  "    GearLayout match at ptrOff=0x%X: nextGear=%u gear=%u topGear=%u "
                  "ratio[0]=%.4f ratio[1]=%.4f",
                  ptrOff, nextGearVal, gearVal, topGearVal,
                  ratios[0], ratios[1]);

        outNextGearOffset = baseGearOffset;
        outGearOffset     = baseGearOffset + 2;
        outTopGearOffset  = baseGearOffset + 6;
        outRatiosOffset   = ptrOff;
        return true;
      }
    }

  } __except (EXCEPTION_EXECUTE_HANDLER) {
    LOG_ERROR(CALIB, "SearchGearLayout: SEH exception while scanning 0x%X-0x%X",
              searchLo, searchHi);
    return false;
  }
  return false;
}

// =============================================================================
// Instance implementation
// =============================================================================

VehicleData::VehicleData(int vehicleHandle) {
  m_vehicle = GameMemory::CVehicle(
      reinterpret_cast<uintptr_t>(getScriptHandleBaseAddress(vehicleHandle)),
      &resolvedOffsets);
  isValid = m_vehicle.IsValid();
}

bool VehicleData::CanRead(uint32_t offset, size_t size) const {
  return isValid && offset != 0;
}

bool VehicleData::CanWrite(uint32_t offset, size_t size) const {
  return isValid && offset != 0;
}

bool VehicleData::IsValid() const { return isValid; }

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

uint8_t VehicleData::GetGear() const { return m_vehicle.GetGear(); }
uint8_t VehicleData::GetNextGear() const { return m_vehicle.GetNextGear(); }
uint8_t VehicleData::GetTopGear() const { return m_vehicle.GetTopGear(); }
float VehicleData::GetClutch() const { return m_vehicle.GetClutch(); }
float VehicleData::GetRPM() const { return m_vehicle.GetRPM(); }
float VehicleData::GetDriveForce() const {
  if (!isValid)
    return 0.0f;
  GameMemory::CHandlingData handling = m_vehicle.GetHandlingData();
  if (handling.IsValid()) {
    return handling.GetDriveForce();
  }
  return 0.0f;
}
float VehicleData::GetFuelLevel() const { return m_vehicle.GetFuelLevel(); }
uint8_t VehicleData::GetLightsBroken() const {
  return m_vehicle.GetLightsBroken();
}
uint8_t VehicleData::GetLightsVisuallyBroken() const {
  return m_vehicle.GetLightsVisuallyBroken();
}
float VehicleData::GetHoverTransformRatioLerp() const {
  return m_vehicle.GetHoverTransformRatioLerp();
}

float VehicleData::GetGearRatio(uint8_t gear) const {
  if (!isValid)
    return 0.0f;
  GameMemory::CHandlingData handling = m_vehicle.GetHandlingData();
  if (handling.IsValid()) {
    return handling.GetGearRatio(gear);
  }
  return 0.0f; // Could not fetch handling
}

bool VehicleData::SetGear(uint8_t gear) {
  if (!isValid)
    return false;
  m_vehicle.SetGear(gear);
  return true;
}

bool VehicleData::SetNextGear(uint8_t gear) {
  if (!isValid)
    return false;
  m_vehicle.SetNextGear(gear);
  return true;
}

bool VehicleData::SetClutch(float clutch) {
  if (!isValid || !std::isfinite(clutch))
    return false;
  m_vehicle.SetClutch(clutch);
  return true;
}

bool VehicleData::SetRPM(float rpm) {
  if (!isValid || !std::isfinite(rpm))
    return false;
  m_vehicle.SetRPM(rpm);
  return true;
}

bool VehicleData::SetLightsBroken(uint8_t brokenState) {
  if (!isValid)
    return false;
  m_vehicle.SetLightsBroken(brokenState);
  return true;
}

bool VehicleData::SetLightsVisuallyBroken(uint8_t brokenState) {
  if (!isValid)
    return false;
  m_vehicle.SetLightsVisuallyBroken(brokenState);
  return true;
}