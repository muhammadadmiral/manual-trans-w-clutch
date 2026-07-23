#pragma once

#include <windows.h>
#include <vector>
#include <string>

class AOBScanner {
public:
    // Finds a pattern in the current process (GTA5.exe) using IDA-style signature
    // Example: "48 8B 05 ? ? ? ? 48 8B 48 08"
    static uintptr_t FindPattern(const char* signature);

private:
    static std::vector<int> ParsePattern(const char* signature);
};
