// =============================================================================
// ModLogger.cpp
// =============================================================================
#include "ModLogger.h"

#define NOMINMAX
#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ModLogger {
namespace {

// ── Internal state ─────────────────────────────────────────────────────────
FILE*      s_file     = nullptr;
Level      s_minLevel = Level::Info;
std::mutex s_mutex;

// Per-call-site throttle table (keyed on the format-string pointer, which is
// stable for string literals compiled into the .text section).
std::unordered_map<const char*, ULONGLONG> s_throttleMap;

// ── String converters ───────────────────────────────────────────────────────
const char* LevelToStr(Level l) {
    switch (l) {
        case Level::Verbose: return "VERBOSE";
        case Level::Info:    return "INFO   ";
        case Level::Warning: return "WARN   ";
        case Level::Err:     return "ERROR  ";
        case Level::Fatal:   return "FATAL  ";
        default:             return "???????";
    }
}

const char* CategoryToStr(Category c) {
    switch (c) {
        case Category::Init:    return "INIT   ";
        case Category::Calib:   return "CALIB  ";
        case Category::Memory:  return "MEMORY ";
        case Category::Gear:    return "GEAR   ";
        case Category::Physics: return "PHYSICS";
        case Category::Input:   return "INPUT  ";
        case Category::Fuel:    return "FUEL   ";
        case Category::Turbo:   return "TURBO  ";
        case Category::Sig:     return "SIGNAL ";
        case Category::Script:  return "SCRIPT ";
        case Category::General: return "GENERAL";
        default:                return "???    ";
    }
}

// Returns  "2026-07-25 15:30:00.123"
std::string Timestamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buf[32]{};
    sprintf_s(buf, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
              st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}

void WriteEntry(Level level, Category category, const char* message) {
    const std::string ts  = Timestamp();
    const char* lv        = LevelToStr(level);
    const char* cat       = CategoryToStr(category);

    char line[1024]{};
    sprintf_s(line, "[%s] [%s] [%s] %s\n", ts.c_str(), lv, cat, message);

    std::lock_guard<std::mutex> lk(s_mutex);

    if (s_file) {
        fputs(line, s_file);
        // Flush immediately on WARNING+ so logs survive crashes.
        if (level >= Level::Warning)
            fflush(s_file);
    }

    // Also send to DebugView / attached debugger.
    OutputDebugStringA(line);
}

} // anonymous namespace

// ── Public API ────────────────────────────────────────────────────────────────
void Initialize(HMODULE pluginModule) {
    char dllPath[MAX_PATH]{};
    DWORD len = pluginModule
        ? GetModuleFileNameA(pluginModule, dllPath, MAX_PATH)
        : GetCurrentDirectoryA(MAX_PATH, dllPath);

    if (len == 0 || len >= MAX_PATH) {
        OutputDebugStringA("[ModLogger] INIT ERROR: could not resolve path\n");
        return;
    }
    // Strip filename to get directory
    if (pluginModule) {
        char* slash = strrchr(dllPath, '\\');
        if (!slash) slash = strrchr(dllPath, '/');
        if (slash) *slash = '\0';
    }

    std::string logPath = std::string(dllPath) + "\\manual-trans.log";

    // Truncate if > 2 MB to prevent disk bloat across sessions.
    {
        FILE* check = nullptr;
        if (fopen_s(&check, logPath.c_str(), "rb") == 0 && check) {
            fseek(check, 0, SEEK_END);
            const long sz = ftell(check);
            fclose(check);
            if (sz > 2 * 1024 * 1024) {
                FILE* trunc = nullptr;
                fopen_s(&trunc, logPath.c_str(), "w");
                if (trunc) {
                    fputs("[ModLogger] Log truncated (>2 MB)\n", trunc);
                    fclose(trunc);
                }
            }
        }
    }

    if (fopen_s(&s_file, logPath.c_str(), "a") != 0 || !s_file) {
        OutputDebugStringA("[ModLogger] ERROR: could not open manual-trans.log\n");
        return;
    }

    WriteEntry(Level::Info, Category::Init,
               "========== manual-trans-w-clutch session start ==========");
}

void Shutdown() {
    if (!s_file) return;
    WriteEntry(Level::Info, Category::Init,
               "========== manual-trans-w-clutch session end   ==========");
    fclose(s_file);
    s_file = nullptr;
}

void SetMinLevel(Level level) { s_minLevel = level; }
Level GetMinLevel()            { return s_minLevel; }

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

    const ULONGLONG now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lk(s_mutex);
        const auto it = s_throttleMap.find(fmt);
        if (it != s_throttleMap.end() &&
            now - it->second < static_cast<ULONGLONG>(cooldownMs))
            return;
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
