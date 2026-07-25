// =============================================================================
// ModLogger.cpp
// =============================================================================
#include "ModLogger.h"
#include "Config.h"

#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <string>
#include <unordered_map>
#include <mutex>

// GTA V / ScriptHookV writes to a file called "ScriptHookV.log" in the game
// folder using OutputDebugString and a proprietary internal handle.  We can't
// write to that file directly, but ANY file written adjacent to the .asi is
// picked up by OpenIV / ASI Manager and readable via DebugView.
//
// We do TWO things:
//   1.  Write our own structured log file ("manual-trans.log") next to the .asi.
//   2.  Call OutputDebugStringA() for every entry so it appears in any
//       attached debugger AND in tools like DebugView with process filtering.

namespace ModLogger {

// ── Internal state ────────────────────────────────────────────────────────────
namespace {
    FILE*   s_file      = nullptr;
    Level   s_minLevel  = Level::INFO;
    std::mutex s_mutex;

    // Per-category, per-message throttle map (key = hashed format string address).
    // Using the format string pointer as a stable key is fine for macros where
    // the string literal lives in the read-only data segment.
    std::unordered_map<const char*, ULONGLONG> s_throttleMap;

    const char* LevelToStr(Level l) {
        switch (l) {
            case Level::DEBUG:  return "DEBUG";
            case Level::INFO:   return "INFO ";
            case Level::WARN:   return "WARN ";
            case Level::ERROR:  return "ERROR";
            case Level::FATAL:  return "FATAL";
            default:            return "?????";
        }
    }

    const char* CategoryToStr(Category c) {
        switch (c) {
            case Category::INIT:    return "INIT   ";
            case Category::CALIB:   return "CALIB  ";
            case Category::MEM:     return "MEM    ";
            case Category::GEAR:    return "GEAR   ";
            case Category::PHYSICS: return "PHYSICS";
            case Category::INPUT:   return "INPUT  ";
            case Category::FUEL:    return "FUEL   ";
            case Category::TURBO:   return "TURBO  ";
            case Category::SIGNAL:  return "SIGNAL ";
            case Category::SCRIPT:  return "SCRIPT ";
            case Category::GENERAL: return "GENERAL";
            default:                return "???    ";
        }
    }

    // Returns a timestamp string like "2026-07-25 15:30:00.123"
    std::string GetTimestamp() {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        char buf[32]{};
        sprintf_s(buf, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        return buf;
    }

    void WriteEntry(Level level, Category category, const char* message) {
        const std::string ts = GetTimestamp();
        const char* lv = LevelToStr(level);
        const char* cat = CategoryToStr(category);

        // Format: [TIMESTAMP] [LEVEL] [CATEGORY] message
        char line[1024]{};
        sprintf_s(line, "[%s] [%s] [%s] %s\n", ts.c_str(), lv, cat, message);

        std::lock_guard<std::mutex> lock(s_mutex);

        // 1. Write to file
        if (s_file) {
            fputs(line, s_file);
            // Flush immediately on WARN+ so logs survive crashes.
            if (level >= Level::WARN)
                fflush(s_file);
        }

        // 2. OutputDebugString for DebugView / debugger / GTAV log aggregators
        OutputDebugStringA(line);
    }

} // namespace

// ── Public API ────────────────────────────────────────────────────────────────
void Initialize(HMODULE pluginModule) {
    char dllPath[MAX_PATH]{};
    DWORD len = pluginModule
        ? GetModuleFileNameA(pluginModule, dllPath, MAX_PATH)
        : GetCurrentDirectoryA(MAX_PATH, dllPath);

    if (len == 0 || len >= MAX_PATH) {
        OutputDebugStringA("[ModLogger] INIT ERROR: could not resolve module path\n");
        return;
    }

    // Strip filename, keep directory
    if (pluginModule) {
        char* slash = strrchr(dllPath, '\\');
        if (!slash) slash = strrchr(dllPath, '/');
        if (slash) *slash = '\0';
    }

    std::string logPath = std::string(dllPath) + "\\manual-trans.log";

    // Open in append mode so consecutive game sessions accumulate.
    // Truncate if the file grows beyond 2 MB to avoid disk bloat.
    {
        FILE* existing = nullptr;
        fopen_s(&existing, logPath.c_str(), "rb");
        if (existing) {
            fseek(existing, 0, SEEK_END);
            long sz = ftell(existing);
            fclose(existing);
            if (sz > 2 * 1024 * 1024) {
                // Overwrite (truncate)
                fopen_s(&s_file, logPath.c_str(), "w");
                if (s_file)
                    fputs("[ModLogger] Log truncated (exceeded 2 MB)\n", s_file);
            }
        }
    }
    if (!s_file)
        fopen_s(&s_file, logPath.c_str(), "a");

    if (!s_file) {
        OutputDebugStringA("[ModLogger] INIT ERROR: could not open log file\n");
        return;
    }

    WriteEntry(Level::INFO, Category::INIT,
               "========== manual-trans-w-clutch session start ==========");
}

void Shutdown() {
    if (s_file) {
        WriteEntry(Level::INFO, Category::INIT,
                   "========== manual-trans-w-clutch session end ===========");
        fclose(s_file);
        s_file = nullptr;
    }
}

void SetMinLevel(Level level) { s_minLevel = level; }
Level GetMinLevel() { return s_minLevel; }

void Log(Level level, Category category, const char* fmt, ...) {
    if (level < s_minLevel) return;

    char message[1024]{};
    va_list args;
    va_start(args, fmt);
    vsnprintf_s(message, sizeof(message), _TRUNCATE, fmt, args);
    va_end(args);

    WriteEntry(level, category, message);
}

void LogThrottled(Level level, Category category, int cooldownMs,
                  const char* fmt, ...) {
    if (level < s_minLevel) return;

    // Use the format string pointer as a cheap stable key.
    ULONGLONG now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_throttleMap.find(fmt);
        if (it != s_throttleMap.end()) {
            if (now - it->second < static_cast<ULONGLONG>(cooldownMs))
                return;
        }
        s_throttleMap[fmt] = now;
    }

    char message[1024]{};
    va_list args;
    va_start(args, fmt);
    vsnprintf_s(message, sizeof(message), _TRUNCATE, fmt, args);
    va_end(args);

    WriteEntry(level, category, message);
}

} // namespace ModLogger
