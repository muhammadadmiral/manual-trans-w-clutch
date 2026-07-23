#pragma once
#include <windows.h>
#include <string>

namespace Config {
    extern int KeyShiftUp;
    extern int KeyShiftDown;
    extern int KeyClutch;
    extern bool DebugOverlay;

    void ReadConfig(HMODULE module);
}
