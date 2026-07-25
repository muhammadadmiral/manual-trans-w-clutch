// =============================================================================
// ModLogger.h  —  Centralized structured logger for manual-trans-w-clutch
//
// Writes categorized, severity-tagged lines to "manual-trans.log" next to
// the .asi.  Also calls OutputDebugStringA() so output appears in DebugView.
//
// ──────────────────────────────────────────────────────────────────────────────
// !! NAMING CONVENTION — DO NOT CHANGE BACK TO ALL_CAPS !!
// Windows SDK header wingdi.h contains:
//     #define ERROR  0
// The preprocessor runs before the C++ parser, so any enum/variable named
// ERROR, WARN, INFO, SIGNAL, etc. in ALL_CAPS risks silent macro substitution
// that causes cascading parse errors across every TU that includes <Windows.h>.
// ALL enum values here use PascalCase to avoid every such collision.
// ──────────────────────────────────────────────────────────────────────────────
//
// Usage (macro always takes an UNQUOTED PascalCase category name):
//   LOG_INFO (Calib,  "Idle candidate count: %d", count);
//   LOG_WARN (Gear,   "Grinding likely — clutch not fully pressed");
//   LOG_ERROR(Memory, "VirtualQuery failed at 0x%llX", addr);
//   LOG_DEBUG(Input,  "throttle=%.3f brake=%.3f clutch=%.3f", t, b, c);
//   LOG_WARN_T(Gear, 2000, "Grind warning (rate-limited)");
// =============================================================================
#pragma once

#define NOMINMAX
#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <string>

namespace ModLogger {

// ── Severity ──────────────────────────────────────────────────────────────────
// PascalCase — see banner comment above for why.
enum class Level : int {
    Verbose = 0, // Frame-by-frame verbose (only when DebugOverlay=1)
    Info    = 1, // Normal operational events
    Warning = 2, // Recoverable anomalies worth investigating
    Err     = 3, // Failures that degrade mod functionality
    Fatal   = 4, // Unrecoverable — mod will disable itself
};

// ── Category ──────────────────────────────────────────────────────────────────
enum class Category : int {
    Init    = 0,  // DllMain / Initialize()
    Calib   = 1,  // Calibration state machine
    Memory  = 2,  // Memory reads / writes / VirtualQuery / AOB
    Gear    = 3,  // Gear logic, shifts, stalls
    Physics = 4,  // Engine braking, clutch heat, wheel lockup
    Input   = 5,  // Keyboard / gamepad raw + smoothed values
    Fuel    = 6,  // Fuel & oil temperature
    Turbo   = 7,  // Turbo boost
    Sig     = 8,  // Turn signals (sein / indicator)
    Script  = 9,  // Main script loop lifecycle
    General = 10, // Catch-all
};

// ── Public API ────────────────────────────────────────────────────────────────
void Initialize(HMODULE pluginModule);
void Shutdown();

// Core log function — prefer the macros below.
void Log(Level level, Category category, const char* fmt, ...);

// Rate-limited: only emits at most once per cooldownMs milliseconds per
// unique call site (keyed on the fmt pointer — stable for string literals).
void LogThrottled(Level level, Category category, int cooldownMs,
                  const char* fmt, ...);

void  SetMinLevel(Level level);
Level GetMinLevel();

} // namespace ModLogger

// ── Convenience macros ────────────────────────────────────────────────────────
// 'cat' must be an unquoted Category member in PascalCase (e.g. Init, Calib).
// The macro pastes it as   ModLogger::Category::<cat>.
#define LOG_DEBUG(cat,  ...) ModLogger::Log(ModLogger::Level::Verbose, ModLogger::Category::cat, __VA_ARGS__)
#define LOG_VERBOSE(cat,...) ModLogger::Log(ModLogger::Level::Verbose, ModLogger::Category::cat, __VA_ARGS__)
#define LOG_INFO(cat,   ...) ModLogger::Log(ModLogger::Level::Info,    ModLogger::Category::cat, __VA_ARGS__)
#define LOG_WARN(cat,   ...) ModLogger::Log(ModLogger::Level::Warning, ModLogger::Category::cat, __VA_ARGS__)
#define LOG_ERROR(cat,  ...) ModLogger::Log(ModLogger::Level::Err,     ModLogger::Category::cat, __VA_ARGS__)
#define LOG_FATAL(cat,  ...) ModLogger::Log(ModLogger::Level::Fatal,   ModLogger::Category::cat, __VA_ARGS__)

// Rate-limited variants
#define LOG_DEBUG_T(cat, ms, ...) ModLogger::LogThrottled(ModLogger::Level::Verbose, ModLogger::Category::cat, ms, __VA_ARGS__)
#define LOG_WARN_T(cat,  ms, ...) ModLogger::LogThrottled(ModLogger::Level::Warning, ModLogger::Category::cat, ms, __VA_ARGS__)
