// =============================================================================
// ModLogger.h
// Centralized structured logger for manual-trans-w-clutch.
//
// Writes categorized, severity-tagged lines to a rolling .log file next to the
// .asi (picked up automatically by ScriptHookV's log viewer and any standard
// GTA V log aggregator like ASI Manager / OpenIV log tail).
//
// Usage:
//   LOG_INFO (CALIB, "Idle candidate count: %d", count);
//   LOG_WARN (GEAR,  "Shifting without clutch, grinding possible");
//   LOG_ERROR(MEM,   "VirtualQuery failed at 0x%llX", addr);
//   LOG_DEBUG(INPUT, "throttle=%.3f brake=%.3f clutch=%.3f", t, b, c);
//
// Categories available: INIT, CALIB, MEM, GEAR, PHYSICS, INPUT, FUEL,
//                        TURBO, SIGNAL, SCRIPT, GENERAL
// =============================================================================
#pragma once

#include <Windows.h>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <string>

namespace ModLogger {

// ── Severity levels ──────────────────────────────────────────────────────────
enum class Level : int {
    DEBUG   = 0,  // Verbose frame-by-frame data (only if DebugOverlay on)
    INFO    = 1,  // Normal operational events
    WARN    = 2,  // Recoverable anomalies
    ERROR   = 3,  // Failures that degrade functionality
    FATAL   = 4,  // Unrecoverable failures, mod will disable itself
};

// ── Log categories ────────────────────────────────────────────────────────────
enum class Category : int {
    INIT    = 0,  // Startup, DllMain, Initialize()
    CALIB   = 1,  // Calibration state machine
    MEM     = 2,  // Memory reads/writes, VirtualQuery, AOB
    GEAR    = 3,  // Gear logic, shifts, stalls
    PHYSICS = 4,  // Engine braking, clutch heat, wheel lockup
    INPUT   = 5,  // Keyboard/gamepad raw + smoothed values
    FUEL    = 6,  // Fuel & oil temperature
    TURBO   = 7,  // Turbo boost
    SIGNAL  = 8,  // Turn signals
    SCRIPT  = 9,  // Main script loop lifecycle
    GENERAL = 10, // Catch-all
};

// ── Public API ────────────────────────────────────────────────────────────────
void Initialize(HMODULE pluginModule);
void Shutdown();

// Core log function. Prefer the macros below.
void Log(Level level, Category category, const char* fmt, ...);

// Rate-limited variant: only emits the message once every `cooldownMs` ms.
// Useful for per-frame warnings that would otherwise spam the log.
void LogThrottled(Level level, Category category, int cooldownMs,
                  const char* fmt, ...);

// Returns the minimum level that will actually be written (default: INFO).
// Set from Config to DEBUG to enable verbose output.
void SetMinLevel(Level level);
Level GetMinLevel();

} // namespace ModLogger

// ── Convenience macros ────────────────────────────────────────────────────────
#define LOG_DEBUG(cat, ...)  ModLogger::Log(ModLogger::Level::DEBUG,  ModLogger::Category::cat, __VA_ARGS__)
#define LOG_INFO(cat, ...)   ModLogger::Log(ModLogger::Level::INFO,   ModLogger::Category::cat, __VA_ARGS__)
#define LOG_WARN(cat, ...)   ModLogger::Log(ModLogger::Level::WARN,   ModLogger::Category::cat, __VA_ARGS__)
#define LOG_ERROR(cat, ...)  ModLogger::Log(ModLogger::Level::ERROR,  ModLogger::Category::cat, __VA_ARGS__)
#define LOG_FATAL(cat, ...)  ModLogger::Log(ModLogger::Level::FATAL,  ModLogger::Category::cat, __VA_ARGS__)

// Rate-limited variants (emitted at most once per cooldownMs milliseconds)
#define LOG_WARN_T(cat, ms, ...)  ModLogger::LogThrottled(ModLogger::Level::WARN,  ModLogger::Category::cat, ms, __VA_ARGS__)
#define LOG_DEBUG_T(cat, ms, ...) ModLogger::LogThrottled(ModLogger::Level::DEBUG, ModLogger::Category::cat, ms, __VA_ARGS__)
