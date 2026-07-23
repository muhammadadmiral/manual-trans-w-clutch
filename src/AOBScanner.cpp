#include "AOBScanner.h"
#include <Psapi.h>
#pragma comment(lib, "psapi.lib")

std::vector<int> AOBScanner::ParsePattern(const char* signature) {
    std::vector<int> pattern;
    size_t len = strlen(signature);
    
    for (size_t i = 0; i < len; i++) {
        if (signature[i] == ' ') continue;
        if (signature[i] == '?') {
            pattern.push_back(-1); // -1 represents wildcard
            if (i + 1 < len && signature[i + 1] == '?') {
                i++; // skip double question marks "?? "
            }
        } else {
            char byteStr[3] = { signature[i], signature[i + 1], '\0' };
            pattern.push_back((int)strtol(byteStr, nullptr, 16));
            i++;
        }
    }
    return pattern;
}

uintptr_t AOBScanner::FindPattern(const char* signature) {
    auto pattern = ParsePattern(signature);
    
    HMODULE hModule = GetModuleHandle(nullptr); // GTA5.exe
    if (!hModule) return 0;
    
    MODULEINFO moduleInfo;
    GetModuleInformation(GetCurrentProcess(), hModule, &moduleInfo, sizeof(moduleInfo));
    
    uint8_t* baseAddress = reinterpret_cast<uint8_t*>(moduleInfo.lpBaseOfDll);
    size_t moduleSize = moduleInfo.SizeOfImage;
    size_t patternSize = pattern.size();

    for (size_t i = 0; i < moduleSize - patternSize; i++) {
        bool found = true;
        for (size_t j = 0; j < patternSize; j++) {
            if (pattern[j] != -1 && baseAddress[i + j] != pattern[j]) {
                found = false;
                break;
            }
        }
        if (found) {
            return reinterpret_cast<uintptr_t>(&baseAddress[i]);
        }
    }
    return 0;
}
