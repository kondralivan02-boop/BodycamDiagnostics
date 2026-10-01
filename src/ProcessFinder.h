#pragma once
#include <Windows.h>
#include <string>

struct ProcessInfo {
    DWORD pid = 0;
    std::wstring exeName;
};

ProcessInfo findProcessByName(const std::wstring& exeName);
