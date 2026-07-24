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
}

CalibrationState VehicleData::GetCalibrationState() { return calibState; }

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
    candidateOffsets.clear();
    for (uint32_t offset = 0x600; offset < 0xC00; offset += 4) {
      if (AOBScanner::IsReadable(calibVehicle.GetAddress() + offset, 4)) {
        float val =
            *reinterpret_cast<float *>(calibVehicle.GetAddress() + offset);
        if (val == 0.0f)
          candidateOffsets.push_back(offset);
      }
    }
    calibState = CalibrationState::WaitingForEngineOn;
  } else if (calibState == CalibrationState::WaitingForEngineOn) {
    if (isEngineOn)
      calibState = CalibrationState::ScanningEngineOn;
  } else if (calibState == CalibrationState::ScanningEngineOn) {
    std::vector<uint32_t> nextCandidates;
    bool foundIdle = false;
    for (uint32_t offset : candidateOffsets) {
      float val =
          *reinterpret_cast<float *>(calibVehicle.GetAddress() + offset);
      if (val > 0.15f && val < 0.35f) {
        nextCandidates.push_back(offset);
        foundIdle = true;
      }
    }
    if (foundIdle) {
      candidateOffsets = nextCandidates;
      calibState = CalibrationState::WaitingForRev;
    }
  } else if (calibState == CalibrationState::WaitingForRev) {
    if (isRevving)
      calibState = CalibrationState::ScanningRev;
  } else if (calibState == CalibrationState::ScanningRev) {
    std::vector<uint32_t> nextCandidates;
    bool foundRev = false;
    for (uint32_t offset : candidateOffsets) {
      float val =
          *reinterpret_cast<float *>(calibVehicle.GetAddress() + offset);
      if (val > 0.4f && val <= 1.2f) {
        nextCandidates.push_back(offset);
        foundRev = true;
      }
    }

    if (foundRev) {
      candidateOffsets = nextCandidates;
      if (!candidateOffsets.empty()) {
        resolvedOffsets.RPM = candidateOffsets[0];
        resolvedOffsets.Clutch = resolvedOffsets.RPM + 12;
        
        bool foundGearLayout = false;
        // The gear offset is usually between 0x20 and 0x60 bytes before RPM.
        // We scan backwards to find a valid Gear structure.
        for (uint32_t offset = resolvedOffsets.RPM - 0x10; offset >= resolvedOffsets.RPM - 0x80; --offset) {
            if (!AOBScanner::IsReadable(calibVehicle.GetAddress() + offset - 8, 32)) continue;
            
            uint8_t gearVal = *reinterpret_cast<uint8_t*>(calibVehicle.GetAddress() + offset);
            
            // When revving from standstill, Gear is usually 1 (sometimes 0 if neutral)
            if (gearVal == 1 || gearVal == 0) {
                // 1. Test Legacy Layout: NextGear=offset-2, TopGear=offset+4, GearRatios=offset+6
                uint8_t nextGearLegacy = *reinterpret_cast<uint8_t*>(calibVehicle.GetAddress() + offset - 2);
                uint8_t topGearLegacy = *reinterpret_cast<uint8_t*>(calibVehicle.GetAddress() + offset + 4);
                uintptr_t ratiosLegacy = *reinterpret_cast<uintptr_t*>(calibVehicle.GetAddress() + offset + 6);

                if ((nextGearLegacy == 0 || nextGearLegacy == 1 || nextGearLegacy == 2) &&
                    (topGearLegacy >= 4 && topGearLegacy <= 9) &&
                    ratiosLegacy > 0x10000000 &&
                    AOBScanner::IsReadable(ratiosLegacy, 4)) {
                    
                    resolvedOffsets.Gear = offset;
                    resolvedOffsets.NextGear = offset - 2;
                    resolvedOffsets.TopGear = offset + 4;
                    resolvedOffsets.GearRatios = offset + 6;
                    foundGearLayout = true;
                    break;
                }

                // 2. Test Enhanced Layout: NextGear=offset+2, TopGear=offset+6, GearRatios=offset+8
                uint8_t nextGearEnhanced = *reinterpret_cast<uint8_t*>(calibVehicle.GetAddress() + offset + 2);
                uint8_t topGearEnhanced = *reinterpret_cast<uint8_t*>(calibVehicle.GetAddress() + offset + 6);
                uintptr_t ratiosEnhanced = *reinterpret_cast<uintptr_t*>(calibVehicle.GetAddress() + offset + 8);

                if ((nextGearEnhanced == 0 || nextGearEnhanced == 1 || nextGearEnhanced == 2) &&
                    (topGearEnhanced >= 4 && topGearEnhanced <= 9) &&
                    ratiosEnhanced > 0x10000000 &&
                    AOBScanner::IsReadable(ratiosEnhanced, 4)) {
                    
                    resolvedOffsets.Gear = offset;
                    resolvedOffsets.NextGear = offset + 2;
                    resolvedOffsets.TopGear = offset + 6;
                    resolvedOffsets.GearRatios = offset + 8;
                    foundGearLayout = true;
                    break;
                }
            }
        }

        if (foundGearLayout) {
            calibState = CalibrationState::Done;
            initialized = true;
            SaveOffsetsToIni(pluginModule, resolvedOffsets);
        } else {
            resolvedOffsets.Gear = 0;
            resolvedOffsets.NextGear = 0;
            calibState = CalibrationState::Failed;
            lastFailureReason =
                "RPM/Clutch located OK, but Gear/NextGear can't be safely "
                "guessed. Tested Legacy & Enhanced memory structures but pointer validation failed. Add offsets to ini manually.";
        }
      } else {
        calibState = CalibrationState::Failed;
        lastFailureReason = "Calibration failed to isolate RPM offset";
      }
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