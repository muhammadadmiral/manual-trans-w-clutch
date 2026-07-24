#include "Config.h"
#include <cstdlib>
#include <cstring>

namespace Config {
int KeyShiftUp = VK_LSHIFT;
int KeyShiftDown = VK_LCONTROL;
int KeyClutch = 0x58; // 'X' key
bool DebugOverlay = true;
bool AllowQuadbikes = true;
bool UseRealClutch = true;
std::vector<int> ExcludedVehicleClasses;

bool OverlayBars = true;
float OverlayPosX = 0.02f;
float OverlayPosY = 0.60f;
float OverlayBarWidth = 0.12f;
float OverlayBarHeight = 0.014f;

bool RequireColdStart = true;
int KeyEngine = 90; // Z key
int KeyMenu = 219;  // [ key

float ThrottleAttack = 0.05f;
float ThrottleRelease = 0.1f;
float BrakeAttack = 0.1f;
float BrakeRelease = 0.2f;
float ClutchAttack = 0.05f;
float ClutchRelease = 0.1f;

namespace {

float ReadFloat(const char *section, const char *key, float fallback,
                const char *iniPath) {
  char buffer[32]{};
  const DWORD count = GetPrivateProfileStringA(section, key, "", buffer,
                                               sizeof(buffer), iniPath);
  if (count == 0)
    return fallback;

  char *end = nullptr;
  const float value = std::strtof(buffer, &end);
  if (end == buffer)
    return fallback;
  return value;
}

// Parses "14,15,16,21" (or "14, 15 16" - commas/spaces both work) into a
// list of ints. Silently stops at the first token it can't parse.
std::vector<int> ParseIntList(const char *text) {
  std::vector<int> values;
  const char *cursor = text;
  while (*cursor != '\0') {
    char *end = nullptr;
    const long value = std::strtol(cursor, &end, 10);
    if (end == cursor)
      break;
    values.push_back(static_cast<int>(value));
    cursor = end;
    while (*cursor == ',' || *cursor == ' ')
      ++cursor;
  }
  return values;
}

} // namespace

void ReadConfig(HMODULE module) {
  char iniPath[MAX_PATH];
  DWORD length = GetModuleFileNameA(module, iniPath, MAX_PATH);
  if (length == 0 || length >= MAX_PATH)
    return;

  char *slash = std::strrchr(iniPath, '\\');
  if (!slash)
    slash = std::strrchr(iniPath, '/');
  if (!slash)
    return;
  *slash = '\0';

  strcat_s(iniPath, "\\manual-trans.ini");

  if (GetFileAttributesA(iniPath) == INVALID_FILE_ATTRIBUTES) {
    // Generate boilerplate INI if it doesn't exist
    SaveConfig(module);
  }

  KeyShiftUp = GetPrivateProfileIntA("Controls", "ShiftUp", VK_LSHIFT, iniPath);
  KeyShiftDown =
      GetPrivateProfileIntA("Controls", "ShiftDown", VK_LCONTROL, iniPath);
  KeyClutch = GetPrivateProfileIntA("Controls", "ClutchKey", 0x58, iniPath);
  KeyEngine = GetPrivateProfileIntA("Controls", "EngineKey", 90, iniPath);
  KeyMenu = GetPrivateProfileIntA("Controls", "MenuKey", 219, iniPath);

  DebugOverlay = GetPrivateProfileIntA("Debug", "Overlay", 1, iniPath) != 0;
  AllowQuadbikes =
      GetPrivateProfileIntA("Vehicles", "AllowQuadbikes", 1, iniPath) != 0;
  UseRealClutch =
      GetPrivateProfileIntA("Vehicles", "UseRealClutch", 1, iniPath) != 0;
  RequireColdStart =
      GetPrivateProfileIntA("Vehicles", "RequireColdStart", 1, iniPath) != 0;

  ThrottleAttack = ReadFloat("Analog", "ThrottleAttack", 0.05f, iniPath);
  ThrottleRelease = ReadFloat("Analog", "ThrottleRelease", 0.10f, iniPath);
  BrakeAttack = ReadFloat("Analog", "BrakeAttack", 0.10f, iniPath);
  BrakeRelease = ReadFloat("Analog", "BrakeRelease", 0.20f, iniPath);
  ClutchAttack = ReadFloat("Analog", "ClutchAttack", 0.05f, iniPath);
  ClutchRelease = ReadFloat("Analog", "ClutchRelease", 0.10f, iniPath);

  char excludedClassesBuffer[128]{};
  GetPrivateProfileStringA("Vehicles", "ExcludedClasses", "",
                           excludedClassesBuffer, sizeof(excludedClassesBuffer),
                           iniPath);
  ExcludedVehicleClasses = ParseIntList(excludedClassesBuffer);

  OverlayBars = GetPrivateProfileIntA("Overlay", "Bars", 1, iniPath) != 0;
  OverlayPosX = ReadFloat("Overlay", "PosX", 0.02f, iniPath);
  OverlayPosY = ReadFloat("Overlay", "PosY", 0.60f, iniPath);
  OverlayBarWidth = ReadFloat("Overlay", "BarWidth", 0.12f, iniPath);
  OverlayBarHeight = ReadFloat("Overlay", "BarHeight", 0.014f, iniPath);
}

void WriteFloat(const char *section, const char *key, float value, const char *iniPath) {
    char buffer[32]{};
    sprintf_s(buffer, "%.3f", value);
    WritePrivateProfileStringA(section, key, buffer, iniPath);
}

void WriteInt(const char *section, const char *key, int value, const char *iniPath) {
    char buffer[32]{};
    sprintf_s(buffer, "%d", value);
    WritePrivateProfileStringA(section, key, buffer, iniPath);
}

void SaveConfig(HMODULE module) {
  char iniPath[MAX_PATH];
  DWORD length = GetModuleFileNameA(module, iniPath, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return;

  char *slash = std::strrchr(iniPath, '\\');
  if (!slash) slash = std::strrchr(iniPath, '/');
  if (!slash) return;
  *slash = '\0';

  strcat_s(iniPath, "\\manual-trans.ini");
  
  WriteInt("Controls", "ShiftUp", KeyShiftUp, iniPath);
  WriteInt("Controls", "ShiftDown", KeyShiftDown, iniPath);
  WriteInt("Controls", "ClutchKey", KeyClutch, iniPath);
  WriteInt("Controls", "EngineKey", KeyEngine, iniPath);
  WriteInt("Controls", "MenuKey", KeyMenu, iniPath);

  WriteInt("Debug", "Overlay", DebugOverlay ? 1 : 0, iniPath);
  WriteInt("Vehicles", "AllowQuadbikes", AllowQuadbikes ? 1 : 0, iniPath);
  WriteInt("Vehicles", "UseRealClutch", UseRealClutch ? 1 : 0, iniPath);
  WriteInt("Vehicles", "RequireColdStart", RequireColdStart ? 1 : 0, iniPath);

  WriteFloat("Analog", "ThrottleAttack", ThrottleAttack, iniPath);
  WriteFloat("Analog", "ThrottleRelease", ThrottleRelease, iniPath);
  WriteFloat("Analog", "BrakeAttack", BrakeAttack, iniPath);
  WriteFloat("Analog", "BrakeRelease", BrakeRelease, iniPath);
  WriteFloat("Analog", "ClutchAttack", ClutchAttack, iniPath);
  WriteFloat("Analog", "ClutchRelease", ClutchRelease, iniPath);

  WriteInt("Overlay", "Bars", OverlayBars ? 1 : 0, iniPath);
  WriteFloat("Overlay", "PosX", OverlayPosX, iniPath);
  WriteFloat("Overlay", "PosY", OverlayPosY, iniPath);
  WriteFloat("Overlay", "BarWidth", OverlayBarWidth, iniPath);
  WriteFloat("Overlay", "BarHeight", OverlayBarHeight, iniPath);
}

} // namespace Config