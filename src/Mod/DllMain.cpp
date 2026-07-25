// =============================================================================
// DllMain.cpp
// Windows DLL entry point for the manual-trans-w-clutch ScriptHookV mod.
//
// Responsibilities (and ONLY these):
//   - Initialize / shut down ModLogger
//   - Register / unregister ScriptMain with ScriptHookV
//   - Expose g_pluginModule so other TUs can find the .asi directory
//
// Everything else lives in src/Script/MainScript.cpp (game loop) or
// src/Core, src/Memory, src/Vehicle (subsystems).
// =============================================================================
#define NOMINMAX
#include <Windows.h>

#include "../../sdk/inc/main.h"
#include "../Core/ModLogger.h"
#include "../Memory/GearboxPatches.h"
#include "../Script/MainScript.h"

HMODULE g_pluginModule = nullptr;

BOOL APIENTRY DllMain(HMODULE instance, DWORD reason, LPVOID) {
    switch (reason) {

    case DLL_PROCESS_ATTACH:
        g_pluginModule = instance;
        DisableThreadLibraryCalls(instance);
        ModLogger::Initialize(instance);
        {
            char modulePath[MAX_PATH]{};
            GetModuleFileNameA(instance, modulePath, MAX_PATH);
            LOG_INFO(Init,
        "Runtime=driveline-r16-crashfix built=%s %s module=%s",
                     __DATE__, __TIME__,
                     modulePath[0] ? modulePath : "?");
        }
        LOG_INFO(Init, "DLL_PROCESS_ATTACH — registering ScriptMain with ScriptHookV");
        scriptRegister(instance, ScriptMain);
        break;

    case DLL_PROCESS_DETACH:
        LOG_INFO(Init, "DLL_PROCESS_DETACH — unregistering script, flushing logs");
        GearboxPatches::Shutdown();
        scriptUnregister(instance);
        ModLogger::Shutdown();
        break;

    default:
        break;
    }
    return TRUE;
}
