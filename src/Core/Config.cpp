#include "Config.h"
#include <cstring>

namespace Config {
    int KeyShiftUp = VK_LSHIFT;
    int KeyShiftDown = VK_LCONTROL;
    int KeyClutch = 0x58; // 'X' key
    bool DebugOverlay = true;

    void ReadConfig(HMODULE module) {
        char iniPath[MAX_PATH];
        DWORD length = GetModuleFileNameA(module, iniPath, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) return;

        char* slash = std::strrchr(iniPath, '\\');
        if (!slash) slash = std::strrchr(iniPath, '/');
        if (!slash) return;
        *slash = '\0';
        
        strcat_s(iniPath, "\\manual-trans.ini");

        KeyShiftUp = GetPrivateProfileIntA("Controls", "ShiftUp", VK_LSHIFT, iniPath);
        KeyShiftDown = GetPrivateProfileIntA("Controls", "ShiftDown", VK_LCONTROL, iniPath);
        KeyClutch = GetPrivateProfileIntA("Controls", "ClutchKey", 0x58, iniPath);
        DebugOverlay = GetPrivateProfileIntA("Debug", "Overlay", 1, iniPath) != 0;
    }
}
