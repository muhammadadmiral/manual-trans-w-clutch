// =============================================================================
// VersionInfo.h — Single source of truth for mod version strings.
// All notifications, logs, and headers MUST reference these constants.
// =============================================================================
#pragma once

namespace VersionInfo {

constexpr const char* kVersion      = "1.1.0";
constexpr const char* kBuildLabel   = "Melar Transmission";
constexpr const char* kFullLabel    = "Melar Transmission v1.1.0";
constexpr const char* kReleaseDate  = "2026";
constexpr bool        kReleaseBuild = true;

// Short label for in-game notifications
constexpr const char* kNotifyPrefix = "Melar Transmission v1.1";

// Log header printed at startup
constexpr const char* kLogBanner =
    "========================================\n"
    " Melar Transmission v1.1.0\n"
    " (c) 2026 — Production Release\n"
    "========================================";

} // namespace VersionInfo
