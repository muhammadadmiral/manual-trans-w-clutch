#include "GearboxPatches.h"
#include "AOBScanner.h"
#include "../Core/ModLogger.h"

#define NOMINMAX
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace GearboxPatches {
namespace {

constexpr size_t kMaxPatchBytes = 13;

struct Patch {
  const char *name;
  const char *signature;
  size_t offset;
  size_t length;
  uintptr_t address = 0;
  std::array<uint8_t, kMaxPatchBytes> original{};
  bool applied = false;
};

// Signature ini milik rutinitas transmisi GTA V Enhanced. Yang diambil cuma
// bentuk instruksinya; patch/rollback dan ownership state tetap punya kita.
std::array<Patch, 5> s_patches{{
    {"shift-up + clutch",
     "75 0D 66 41 FF 45 ? 41 C7 45 54 CD CC CC 3D 41 C7 85 ? 00 00 00 "
     "00 00 00 00",
     2, 13},
    {"shift-down + clutch",
     "75 0D 66 41 FF 4D ? 41 C7 45 54 CD CC CC 3D 66 41 C7 45 04 06 00",
     0, 2},
    {"clutch low RPM", "C7 43 ? CD CC CC 3D 66", 0, 7},
    {"clutch rev limit",
     "C7 43 ? CD CC CC 3D 44 89 ? ? ? ? ? 44 89 ? ? ? ? ?",
     0, 7},
    {"throttle lift", "89 4F 58 F3 44 0F 11", 0, 3},
}};

bool s_resolveAttempted = false;
bool s_resolved = false;
bool s_applied = false;
std::string s_failure;

bool IsExactBytes(uintptr_t address, const uint8_t *bytes, size_t length) {
  if (!AOBScanner::IsReadable(address, length))
    return false;
  __try {
    return std::memcmp(reinterpret_cast<const void *>(address), bytes,
                       length) == 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool IsNopped(uintptr_t address, size_t length) {
  if (!AOBScanner::IsReadable(address, length))
    return false;
  __try {
    const auto *bytes = reinterpret_cast<const uint8_t *>(address);
    for (size_t i = 0; i < length; ++i) {
      if (bytes[i] != 0x90)
        return false;
    }
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool CopyFromAddress(uintptr_t address, uint8_t *destination, size_t length) {
  if (!AOBScanner::IsReadable(address, length))
    return false;
  __try {
    std::memcpy(destination, reinterpret_cast<const void *>(address), length);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool WriteBytes(uintptr_t address, const uint8_t *bytes, size_t length) {
  DWORD oldProtection = 0;
  if (!VirtualProtect(reinterpret_cast<void *>(address), length,
                      PAGE_EXECUTE_READWRITE, &oldProtection)) {
    return false;
  }

  bool copied = false;
  __try {
    std::memcpy(reinterpret_cast<void *>(address), bytes, length);
    copied = true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    copied = false;
  }

  FlushInstructionCache(GetCurrentProcess(),
                        reinterpret_cast<const void *>(address), length);
  DWORD ignored = 0;
  const bool protectionRestored =
      VirtualProtect(reinterpret_cast<void *>(address), length, oldProtection,
                     &ignored) != FALSE;
  return copied && protectionRestored;
}

bool WriteNops(uintptr_t address, size_t length) {
  std::array<uint8_t, kMaxPatchBytes> nops{};
  nops.fill(0x90);
  return length <= nops.size() && WriteBytes(address, nops.data(), length);
}

bool ResolveAll() {
  if (s_resolveAttempted)
    return s_resolved;
  s_resolveAttempted = true;

  for (auto &patch : s_patches) {
    const auto matches = AOBScanner::FindAll(patch.signature, 2);
    if (matches.size() != 1) {
      s_failure = std::string(patch.name) +
                  (matches.empty() ? " signature tidak ditemukan"
                                   : " signature tidak unik");
      LOG_ERROR(Memory, "Gearbox patch resolve gagal: %s",
                s_failure.c_str());
      return false;
    }

    patch.address = matches.front() + patch.offset;
    if (patch.length == 0 || patch.length > patch.original.size() ||
        !AOBScanner::IsReadable(patch.address, patch.length)) {
      s_failure = std::string(patch.name) + " target tidak aman dibaca";
      LOG_ERROR(Memory, "Gearbox patch resolve gagal: %s",
                s_failure.c_str());
      return false;
    }

    LOG_INFO(Memory, "Gearbox patch resolved: %s=%p offset=%zu len=%zu",
             patch.name, reinterpret_cast<void *>(patch.address), patch.offset,
             patch.length);

    if (!CopyFromAddress(patch.address, patch.original.data(),
                         patch.length)) {
      s_failure = std::string(patch.name) + " gagal menyimpan byte asli";
      LOG_ERROR(Memory, "Gearbox patch resolve gagal: %s",
                s_failure.c_str());
      return false;
    }
  }

  s_resolved = true;
  LOG_INFO(Memory,
           "Gearbox native Enhanced resolved: up=%p down=%p low=%p "
           "rev=%p lift=%p",
           reinterpret_cast<void *>(s_patches[0].address),
           reinterpret_cast<void *>(s_patches[1].address),
           reinterpret_cast<void *>(s_patches[2].address),
           reinterpret_cast<void *>(s_patches[3].address),
           reinterpret_cast<void *>(s_patches[4].address));
  return true;
}

bool RestoreApplied() {
  bool restored = true;
  bool anyStillApplied = false;
  for (auto it = s_patches.rbegin(); it != s_patches.rend(); ++it) {
    Patch &patch = *it;
    if (!patch.applied)
      continue;

    if (!IsNopped(patch.address, patch.length)) {
      LOG_ERROR(Memory,
                "Gearbox patch '%s' berubah setelah dipasang; rollback "
                "dibatalkan supaya tidak menimpa mod lain",
                patch.name);
      restored = false;
      anyStillApplied = true;
      continue;
    }
    if (!WriteBytes(patch.address, patch.original.data(), patch.length)) {
      LOG_ERROR(Memory, "Gearbox patch '%s' gagal direstore", patch.name);
      restored = false;
      anyStillApplied = true;
      continue;
    }
    patch.applied = false;
  }
  s_applied = anyStillApplied;
  return restored;
}

bool ApplyAll() {
  if (s_applied)
    return true;
  if (!ResolveAll())
    return false;

  for (auto &patch : s_patches) {
    if (!IsExactBytes(patch.address, patch.original.data(), patch.length)) {
      s_failure = std::string(patch.name) +
                  " byte target berubah sebelum aktivasi";
      LOG_ERROR(Memory, "Gearbox patch batal: %s", s_failure.c_str());
      RestoreApplied();
      return false;
    }
    const bool writeReportedSuccess =
        WriteNops(patch.address, patch.length);
    const bool targetIsNopped = IsNopped(patch.address, patch.length);
    patch.applied = targetIsNopped;
    if (!writeReportedSuccess || !targetIsNopped) {
      s_failure = std::string(patch.name) + " gagal ditulis";
      LOG_ERROR(Memory, "Gearbox patch batal: %s", s_failure.c_str());
      RestoreApplied();
      return false;
    }
  }

  s_applied = true;
  s_failure.clear();
  LOG_INFO(Memory,
           "Gearbox native override aktif (5/5, atomic, velocity untouched)");
  return true;
}

} // namespace

bool SetActive(bool active) {
  if (active)
    return ApplyAll();
  if (!s_applied)
    return true;

  const bool restored = RestoreApplied();
  if (restored)
    LOG_INFO(Memory, "Gearbox native override direstore (5/5)");
  return restored;
}

void Shutdown() {
  if (s_applied)
    RestoreApplied();
}

bool IsApplied() { return s_applied; }
bool IsResolved() { return s_resolved; }
const char *GetFailureReason() {
  return s_failure.empty() ? "" : s_failure.c_str();
}

} // namespace GearboxPatches
