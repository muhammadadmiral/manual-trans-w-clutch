// =============================================================================
// DllMain.cpp
// Windows DLL entry point for the manual-trans-w-clutch ScriptHookV mod.
//
// Responsibilities:
//   - Initialize / shut down ModLogger
//   - Register / unregister ScriptMain with ScriptHookV
//
// Nothing else belongs here. Keep this file minimal.
// =============================================================================
#define NOMINMAX
#include <Windows.h>

#include "../../sdk/inc/main.h"
#include "../Core/ModLogger.h"
#include "../Script/MainScript.h"

// Exposed so other translation units (MainScript, VehicleData, Config, …)
// can pass the HMODULE to APIs like GetModuleFileNameA.
HMODULE g_pluginModule = nullptr;

BOOL APIENTRY DllMain(HMODULE instance, DWORD reason, LPVOID) {
    switch (reason) {

    case DLL_PROCESS_ATTACH:
        g_pluginModule = instance;
        DisableThreadLibraryCalls(instance);

        // Initialize the logger first so every subsequent call can use it.
        ModLogger::Initialize(instance);
        LOG_INFO(INIT, "DLL_PROCESS_ATTACH — registering script thread with ScriptHookV.");

        scriptRegister(instance, ScriptMain);
        break;

    case DLL_PROCESS_DETACH:
        LOG_INFO(INIT, "DLL_PROCESS_DETACH — unregistering script and flushing logs.");
        scriptUnregister(instance);
        ModLogger::Shutdown();
        break;

    default:
        break;
    }

    return TRUE;
}
