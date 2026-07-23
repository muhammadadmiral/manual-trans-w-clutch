#include <windows.h>
#include "main.h"
#include "natives.h"

void ScriptMain() {
    WAIT(2000);

    while (true) {
        WAIT(0);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        scriptRegister(hModule, ScriptMain);
        break;
    case DLL_PROCESS_DETACH:
        scriptUnregister(hModule);
        break;
    }
    return TRUE;
}