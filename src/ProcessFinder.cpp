#include "ProcessFinder.h"
#include <TlHelp32.h>

ProcessInfo findProcessByName(const std::wstring& exeName) {
    ProcessInfo out{};
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName.c_str()) == 0) {
                out.pid = pe.th32ProcessID;
                out.exeName = pe.szExeFile;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}
