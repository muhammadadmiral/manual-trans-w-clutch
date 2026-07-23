#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class AOBScanner {
public:
    // Scans executable PE sections of the main module (GTA5.exe).
    // Returns zero when the signature is absent or appears more than once.
    static uintptr_t FindUnique(const char* signature);

    // Returns up to maxMatches addresses. Mainly useful for diagnostics.
    static std::vector<uintptr_t> FindAll(const char* signature,
                                          size_t maxMatches = 2);

    static bool IsReadable(uintptr_t address, size_t size);

private:
    static bool ParsePattern(const char* signature,
                             std::vector<int>& pattern);
};
