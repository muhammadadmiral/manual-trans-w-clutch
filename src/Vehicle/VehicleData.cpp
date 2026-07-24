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
  if (!BuildIniPath(pluginModule, iniPath))
    return;

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
  WritePrivateProfileStringA("Offsets", "LightsVisuallyBroken", buffer,
                             iniPath);
  sprintf_s(buffer, "0x%X", offsets.HoverTransformRatioLerp);
  WritePrivateProfileStringA("Offsets", "HoverTransformRatioLerp", buffer,
                             iniPath);
  sprintf_s(buffer, "0x%X", offsets.GearRatios);
  WritePrivateProfileStringA("Offsets", "GearRatios", buffer, iniPath);

  // Save versioned
  const std::string buildVersion = GetGameBuildVersion();
  if (!buildVersion.empty()) {
    std::string versionedSection = "Offsets." + buildVersion;
    sprintf_s(buffer, "0x%X", offsets.Gear);
    WritePrivateProfileStringA(versionedSection.c_str(), "Gear", buffer,
                               iniPath);
    sprintf_s(buffer, "0x%X", offsets.NextGear);
    WritePrivateProfileStringA(versionedSection.c_str(), "NextGear", buffer,
                               iniPath);
    sprintf_s(buffer, "0x%X", offsets.Clutch);
    WritePrivateProfileStringA(versionedSection.c_str(), "Clutch", buffer,
                               iniPath);
    sprintf_s(buffer, "0x%X", offsets.RPM);
    WritePrivateProfileStringA(versionedSection.c_str(), "RPM", buffer,
                               iniPath);
    sprintf_s(buffer, "0x%X", offsets.TopGear);
    WritePrivateProfileStringA(versionedSection.c_str(), "TopGear", buffer,
                               iniPath);
    sprintf_s(buffer, "0x%X", offsets.DriveForce);
    WritePrivateProfileStringA(versionedSection.c_str(), "DriveForce", buffer,
                               iniPath);
    sprintf_s(buffer, "0x%X", offsets.FuelLevel);
    WritePrivateProfileStringA(versionedSection.c_str(), "FuelLevel", buffer,
                               iniPath);
    sprintf_s(buffer, "0x%X", offsets.LightsBroken);
    WritePrivateProfileStringA(versionedSection.c_str(), "LightsBroken", buffer,
                               iniPath);
    sprintf_s(buffer, "0x%X", offsets.LightsVisuallyBroken);
    WritePrivateProfileStringA(versionedSection.c_str(), "LightsVisuallyBroken",
                               buffer, iniPath);
    sprintf_s(buffer, "0x%X", offsets.HoverTransformRatioLerp);
    WritePrivateProfileStringA(versionedSection.c_str(),
                               "HoverTransformRatioLerp", buffer, iniPath);
    sprintf_s(buffer, "0x%X", offsets.GearRatios);
    WritePrivateProfileStringA(versionedSection.c_str(), "GearRatios", buffer,
                               iniPath);
  }

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

    // Save automatically if successfully pattern scanned
    SaveOffsetsToIni(pluginModule, candidate);
    return true;
  }

  if (LoadOffsetsFromIni(pluginModule, candidate)) {
    resolvedOffsets = candidate;
    offsetSource = VehicleOffsetSource::IniFallback;
    initialized = true;
    return true;
  }

  offsetSource = VehicleOffsetSource::Calibration;
  initialized = false;

  if (calibState == CalibrationState::None) {
    calibState = CalibrationState::WaitingForEngineOff;
  }

  return true;
}

void VehicleData::ResetCalibration() {
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
    if (!isEngineOn)
      calibState = CalibrationState::ScanningEngineOff;
  } else if (calibState == CalibrationState::ScanningEngineOff) {
    if (isEngineOn) {
      // Changed their mind / engine came back on mid-wait. Start over.
      calibState = CalibrationState::WaitingForEngineOff;
      phaseEnterTick = 0;
      return;
    }
    const uint64_t now = GetTickCount64();
    if (phaseEnterTick == 0) {
      phaseEnterTick = now;
      return; // just arrived - let RPM actually decay before snapshotting
    }
    if (now - phaseEnterTick < kEngineOffSettleMs)
      return; // still settling, try again next frame

    phaseEnterTick = 0;
    candidateOffsets.clear();
    for (uint32_t offset = kCalibScanStart; offset < kCalibScanEnd;
         offset += 4) {
      if (AOBScanner::IsReadable(calibVehicle.GetAddress() + offset, 4)) {
        float val =
            *reinterpret_cast<float *>(calibVehicle.GetAddress() + offset);
        if (val == 0.0f)
          candidateOffsets.push_back(offset);
      }
    }
    if (candidateOffsets.empty()) {
      calibState = CalibrationState::Failed;
      char msg[160]{};
      sprintf_s(msg,
                "0 candidates in 0x%X-0x%X with engine off. Field is outside "
                "this window - widen kCalibScanStart/End in VehicleData.h.",
                kCalibScanStart, kCalibScanEnd);
      lastFailureReason = msg;
    } else {
      calibState = CalibrationState::WaitingForEngineOn;
    }
  } else if (calibState == CalibrationState::WaitingForEngineOn) {
    if (isEngineOn)
      calibState = CalibrationState::ScanningEngineOn;
  } else if (calibState == CalibrationState::ScanningEngineOn) {
    if (!isEngineOn) {
      calibState = CalibrationState::WaitingForEngineOn;
      phaseEnterTick = 0;
      return;
    }
    const uint64_t now = GetTickCount64();
    if (phaseEnterTick == 0) {
      phaseEnterTick = now;
      return; // let RPM settle at idle before reading it
    }
    if (now - phaseEnterTick < kIdleSettleMs)
      return;

    phaseEnterTick = 0;
    std::vector<uint32_t> nextCandidates;
    std::vector<float> nextIdleValues;
    for (uint32_t offset : candidateOffsets) {
      float val =
          *reinterpret_cast<float *>(calibVehicle.GetAddress() + offset);
      // Idle candidate: moved away from the 0.0 it had with the engine off,
      // by a real (non-noise) amount, but not some unrelated huge counter.
      // Deliberately NOT a fixed 0.1-0.4-style band - RPM's numeric scale
      // isn't guaranteed to be the same "0-1 normalized" convention on
      // every build/game version.
      if (val > 0.02f && val < 50.0f) {
        nextCandidates.push_back(offset);
        nextIdleValues.push_back(val);
      }
    }
    if (!nextCandidates.empty()) {
      candidateOffsets = nextCandidates;
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
    }
  } else if (calibState == CalibrationState::WaitingForRev) {
    if (isRevving)
      calibState = CalibrationState::ScanningRev;
  } else if (calibState == CalibrationState::ScanningRev) {
    if (!isRevving) {
      // They let off the gas before it settled - go back and wait for a
      // sustained rev instead of scanning a mid-climb value.
      calibState = CalibrationState::WaitingForRev;
      phaseEnterTick = 0;
      return;
    }
    const uint64_t now = GetTickCount64();
    if (phaseEnterTick == 0) {
      phaseEnterTick = now;
      return; // let RPM actually climb before reading it
    }
    if (now - phaseEnterTick < kRevSettleMs)
      return;

    phaseEnterTick = 0;
    std::vector<uint32_t> nextCandidates;
    std::vector<float> nextIdleValues;
    bool foundRev = false;
    for (size_t i = 0; i < candidateOffsets.size(); ++i) {
      const uint32_t offset = candidateOffsets[i];
      const float idleVal = candidateIdleValues[i];
      float val =
          *reinterpret_cast<float *>(calibVehicle.GetAddress() + offset);
      // RPM under load should sit meaningfully above its own idle value -
      // relative to itself, not to a fixed absolute band. This is what
      // makes it work regardless of whether RPM is stored 0-1 normalized,
      // as raw revs, or some other scale on this build.
      if (val > idleVal * 1.4f && (val - idleVal) > 0.02f) {
        nextCandidates.push_back(offset);
        nextIdleValues.push_back(idleVal);
        foundRev = true;
      }
    }

    if (foundRev) {
      candidateOffsets = nextCandidates;
      if (!candidateOffsets.empty()) {
        resolvedOffsets.RPM = candidateOffsets[0];
        resolvedOffsets.Clutch = resolvedOffsets.RPM + 12;

        bool foundGearLayout = false;
        uint32_t foundGearOffset = 0, foundNextGearOffset = 0,
                 foundTopGearOffset = 0, foundRatiosOffset = 0;

        // Gear-cluster search: instead of assuming one fixed historical
        // byte layout (the old code only ever tried two exact patterns),
        // scan a window around RPM for a byte that plausibly reads "Gear"
        // (0 or 1, since we're revving from a standstill), then search its
        // immediate neighborhood independently for a NextGear-like byte, a
        // TopGear-like byte, and a pointer to something that actually looks
        // like an array of gear ratios (finite floats in a sane range) -
        // rather than just "a big-looking number". No fixed byte gap
        // between the four fields is assumed, so this should survive
        // layout differences across builds/game versions better than
        // hardcoded deltas would.
        const uint32_t rpmOff = resolvedOffsets.RPM;
        const uint32_t searchLo =
            rpmOff > kGearSearchBefore ? rpmOff - kGearSearchBefore : 0;
        const uint32_t searchHi = rpmOff + kGearSearchAfter;
        // One bulk readability check up front instead of one VirtualQuery
        // per byte - keeps this from causing a frame hitch.
        const bool regionReadable = AOBScanner::IsReadable(
            calibVehicle.GetAddress() + searchLo, (searchHi - searchLo) + 32);

        if (regionReadable) {
          __try {
            for (uint32_t gearOff = searchLo;
                 gearOff <= searchHi && !foundGearLayout; ++gearOff) {
              const uint8_t gearVal = *reinterpret_cast<uint8_t *>(
                  calibVehicle.GetAddress() + gearOff);
              if (gearVal != 0 && gearVal != 1)
                continue;

              uint32_t nextGearOff = 0, topGearOff = 0, ratiosOff = 0;
              bool haveNext = false, haveTop = false, haveRatios = false;

              for (int32_t d = -16; d <= 16 && !haveNext; ++d) {
                if (d == 0 || static_cast<int64_t>(gearOff) + d < 0)
                  continue;
                const uint32_t off =
                    static_cast<uint32_t>(static_cast<int64_t>(gearOff) + d);
                const uint8_t v = *reinterpret_cast<uint8_t *>(
                    calibVehicle.GetAddress() + off);
                if (v <= 2) {
                  nextGearOff = off;
                  haveNext = true;
                }
              }
              for (int32_t d = -24; d <= 24 && !haveTop; ++d) {
                if (d == 0 || static_cast<int64_t>(gearOff) + d < 0)
                  continue;
                const uint32_t off =
                    static_cast<uint32_t>(static_cast<int64_t>(gearOff) + d);
                const uint8_t v = *reinterpret_cast<uint8_t *>(
                    calibVehicle.GetAddress() + off);
                if (v >= 4 && v <= 9) {
                  topGearOff = off;
                  haveTop = true;
                }
              }
              for (int32_t d = -64; d <= 64 && !haveRatios; d += 4) {
                if (static_cast<int64_t>(gearOff) + d < 8)
                  continue;
                const uint32_t off =
                    static_cast<uint32_t>(static_cast<int64_t>(gearOff) + d);
                const uintptr_t ptr = *reinterpret_cast<uintptr_t *>(
                    calibVehicle.GetAddress() + off);
                if (ptr < 0x10000000 || !AOBScanner::IsReadable(ptr, 16))
                  continue;
                const float *ratios = reinterpret_cast<const float *>(ptr);
                bool plausible = true;
                for (int g = 0; g < 4; ++g) {
                  if (!std::isfinite(ratios[g]) || ratios[g] <= 0.0f ||
                      ratios[g] > 20.0f) {
                    plausible = false;
                    break;
                  }
                }
                if (plausible) {
                  ratiosOff = off;
                  haveRatios = true;
                }
              }

              if (haveNext && haveTop && haveRatios) {
                foundGearOffset = gearOff;
                foundNextGearOffset = nextGearOff;
                foundTopGearOffset = topGearOff;
                foundRatiosOffset = ratiosOff;
                foundGearLayout = true;
              }
            }
          } __except (EXCEPTION_EXECUTE_HANDLER) {
            foundGearLayout = false;
          }
        }

        if (foundGearLayout) {
          resolvedOffsets.Gear = foundGearOffset;
          resolvedOffsets.NextGear = foundNextGearOffset;
          resolvedOffsets.TopGear = foundTopGearOffset;
          resolvedOffsets.GearRatios = foundRatiosOffset;
        }

        if (foundGearLayout && AreOffsetsSane(resolvedOffsets)) {
          calibState = CalibrationState::Done;
          initialized = true;
          SaveOffsetsToIni(pluginModule, resolvedOffsets);
        } else if (foundGearLayout) {
          // Layout matched the Legacy/Enhanced shape check, but the final
          // numbers still don't pass the same sanity bounds AOB/INI offsets
          // must pass. Treat it as a failure instead of silently arming a
          // bad offset set that will just get rejected every frame by
          // HasPlausibleLayout() later.
          resolvedOffsets.Gear = 0;
          resolvedOffsets.NextGear = 0;
          calibState = CalibrationState::Failed;
          lastFailureReason =
              "Gear layout matched a known shape but failed the general "
              "sanity check (offset too small/large or fields too far "
              "apart). Recalibrate, or if it repeats, this build's layout "
              "may need a 3rd pattern added to the Legacy/Enhanced test.";
        } else {
          resolvedOffsets.Gear = 0;
          resolvedOffsets.NextGear = 0;
          calibState = CalibrationState::Failed;
          lastFailureReason =
              "RPM/Clutch located OK, but Gear/NextGear can't be safely "
              "guessed. Tested Legacy & Enhanced memory structures but pointer "
              "validation failed. Add offsets to ini manually.";
        }
      } else {
        calibState = CalibrationState::Failed;
        lastFailureReason = "Calibration failed to isolate RPM offset";
      }
    } else {
      // No candidate climbed meaningfully above its own idle value.
      calibState = CalibrationState::Failed;
      lastFailureReason =
          "Held revs for over a second but no idle-candidate rose above its "
          "own idle value by 40%+. Either it wasn't revving hard enough to "
          "register (try holding W harder/longer), or the idle-stage "
          "candidates weren't RPM at all - recalibrate from scratch.";
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