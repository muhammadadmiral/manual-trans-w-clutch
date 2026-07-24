#include "AOBScanner.h"

#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace {

bool IsReadableProtection(DWORD protection) {
  if ((protection & PAGE_GUARD) != 0 || (protection & PAGE_NOACCESS) != 0) {
    return false;
  }

  const DWORD baseProtection = protection & 0xFF;
  switch (baseProtection) {
  case PAGE_READONLY:
  case PAGE_READWRITE:
  case PAGE_WRITECOPY:
  case PAGE_EXECUTE_READ:
  case PAGE_EXECUTE_READWRITE:
  case PAGE_EXECUTE_WRITECOPY:
    return true;
  default:
    return false;
  }
}

int HexValue(char value) {
  if (value >= '0' && value <= '9')
    return value - '0';
  value = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
  if (value >= 'A' && value <= 'F')
    return value - 'A' + 10;
  return -1;
}

} // namespace

bool AOBScanner::ParsePattern(const char *signature,
                              std::vector<int> &pattern) {
  pattern.clear();
  if (signature == nullptr)
    return false;

  const char *cursor = signature;
  while (*cursor != '\0') {
    while (*cursor != '\0' &&
           std::isspace(static_cast<unsigned char>(*cursor))) {
      ++cursor;
    }

    if (*cursor == '\0')
      break;

    if (*cursor == '?') {
      pattern.push_back(-1);
      ++cursor;
      if (*cursor == '?')
        ++cursor;
      continue;
    }

    const int high = HexValue(cursor[0]);
    const int low = cursor[1] != '\0' ? HexValue(cursor[1]) : -1;
    if (high < 0 || low < 0) {
      pattern.clear();
      return false;
    }

    pattern.push_back((high << 4) | low);
    cursor += 2;
  }

  return !pattern.empty();
}

bool AOBScanner::IsReadable(uintptr_t address, size_t size) {
  if (address == 0 || size == 0)
    return false;

  const uintptr_t startPage = address & ~static_cast<uintptr_t>(4095);
  const uintptr_t endPage = (address + size - 1) & ~static_cast<uintptr_t>(4095);

  for (uintptr_t page = startPage; page <= endPage; page += 4096) {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<const void *>(page), &info, sizeof(info)) == 0) {
      return false;
    }
    if (info.State != MEM_COMMIT || !IsReadableProtection(info.Protect)) {
      return false;
    }
  }

  return true;
}

static bool ComparePatternSafe(const uint8_t *data, const std::vector<int> &pattern) {
  __try {
    const size_t patternSize = pattern.size();
    for (size_t i = 0; i < patternSize; ++i) {
      const int expected = pattern[i];
      if (expected != -1 && data[i] != static_cast<uint8_t>(expected)) {
        return false;
      }
    }
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

std::vector<uintptr_t> AOBScanner::FindAll(const char *signature,
                                           size_t maxMatches) {
  std::vector<uintptr_t> matches;
  if (maxMatches == 0)
    return matches;

  std::vector<int> pattern;
  if (!ParsePattern(signature, pattern))
    return matches;

  HMODULE module = GetModuleHandleW(nullptr);
  if (module == nullptr)
    return matches;

  auto *moduleBase = reinterpret_cast<uint8_t *>(module);
  auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(moduleBase);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    return matches;

  auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(moduleBase + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE)
    return matches;

  const size_t patternSize = pattern.size();
  auto *section = IMAGE_FIRST_SECTION(nt);

  for (WORD sectionIndex = 0; sectionIndex < nt->FileHeader.NumberOfSections;
       ++sectionIndex, ++section) {
    const DWORD flags = section->Characteristics;
    if ((flags & IMAGE_SCN_MEM_EXECUTE) == 0 ||
        (flags & IMAGE_SCN_MEM_READ) == 0) {
      continue;
    }

    auto *sectionBase = moduleBase + section->VirtualAddress;
    size_t sectionSize = section->Misc.VirtualSize != 0
                             ? section->Misc.VirtualSize
                             : section->SizeOfRawData;
    const size_t imageRemaining =
        nt->OptionalHeader.SizeOfImage - section->VirtualAddress;
    sectionSize = std::min(sectionSize, imageRemaining);

    if (sectionSize < patternSize) {
      continue;
    }

    const size_t lastStart = sectionSize - patternSize;
    for (size_t offset = 0; offset <= lastStart; ++offset) {
      const uint8_t *candidate = sectionBase + offset;
      if (ComparePatternSafe(candidate, pattern)) {
        matches.push_back(reinterpret_cast<uintptr_t>(candidate));
        if (matches.size() >= maxMatches)
          return matches;
      }
    }
  }

  return matches;
}

uintptr_t AOBScanner::FindUnique(const char *signature) {
  const auto matches = FindAll(signature, 2);
  return matches.size() == 1 ? matches.front() : 0;
}
