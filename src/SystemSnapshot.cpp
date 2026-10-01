#include "SystemSnapshot.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <sstream>

namespace {
std::wstring jsonEscape(const std::wstring& text) {
    std::wstring out;
    for (wchar_t c : text) {
        switch (c) {
        case L'\\': out += L"\\\\"; break;
        case L'\"': out += L"\\\""; break;
        case L'\n': out += L"\\n"; break;
        case L'\r': out += L"\\r"; break;
        case L'\t': out += L"\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::wstring readRegString(HKEY root, const wchar_t* subKey, const wchar_t* value) {
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegGetValueW(root, subKey, value, RRF_RT_REG_SZ, &type, nullptr, &bytes) != ERROR_SUCCESS || bytes == 0)
        return L"";
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t));
    if (RegGetValueW(root, subKey, value, RRF_RT_REG_SZ, &type, buffer.data(), &bytes) != ERROR_SUCCESS)
        return L"";
    return buffer.data();
}

std::wstring cpuModel() {
    return readRegString(
        HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        L"ProcessorNameString");
}

std::wstring nvidiaDriverVersion() {
    HMODULE nvml = LoadLibraryW(L"nvml.dll");
    if (!nvml) return L"";

    using InitFn = int (*)();
    using ShutdownFn = int (*)();
    using DriverFn = int (*)(char*, unsigned int);

    const auto init = reinterpret_cast<InitFn>(GetProcAddress(nvml, "nvmlInit_v2"));
    const auto shutdown = reinterpret_cast<ShutdownFn>(GetProcAddress(nvml, "nvmlShutdown"));
    const auto driver = reinterpret_cast<DriverFn>(GetProcAddress(nvml, "nvmlSystemGetDriverVersion"));

    std::wstring result;
    if (init && shutdown && driver && init() == 0) {
        char buffer[128]{};
        if (driver(buffer, static_cast<unsigned int>(sizeof(buffer))) == 0) {
            const int count = MultiByteToWideChar(CP_UTF8, 0, buffer, -1, nullptr, 0);
            if (count > 1) {
                std::vector<wchar_t> wide(static_cast<std::size_t>(count));
                MultiByteToWideChar(CP_UTF8, 0, buffer, -1, wide.data(), count);
                result = wide.data();
            }
        }
        shutdown();
    }

    FreeLibrary(nvml);
    return result;
}

std::vector<std::wstring> displayAdapters() {
    std::vector<std::wstring> names;
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
        if ((dd.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) == 0 && dd.DeviceString[0] != L'\0') {
            names.emplace_back(dd.DeviceString);
        }
        dd = {};
        dd.cb = sizeof(dd);
    }
    return names;
}

std::vector<std::wstring> activeDisplaysJson() {
    std::vector<std::wstring> items;

    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);

    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
        if ((dd.StateFlags & DISPLAY_DEVICE_ACTIVE) == 0 ||
            (dd.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0) {
            dd = {};
            dd.cb = sizeof(dd);
            continue;
        }

        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);

        if (EnumDisplaySettingsExW(
                dd.DeviceName,
                ENUM_CURRENT_SETTINGS,
                &mode,
                0)) {
            std::wstringstream ss;
            ss << L"{"
               << L"\"device\":\"" << jsonEscape(dd.DeviceName) << L"\","
               << L"\"name\":\"" << jsonEscape(dd.DeviceString) << L"\","
               << L"\"width\":" << mode.dmPelsWidth << L","
               << L"\"height\":" << mode.dmPelsHeight << L","
               << L"\"refresh_hz\":" << mode.dmDisplayFrequency << L","
               << L"\"primary\":"
               << ((dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) ? L"true" : L"false")
               << L"}";
            items.push_back(ss.str());
        }

        dd = {};
        dd.cb = sizeof(dd);
    }

    return items;
}
}

bool writeSystemSnapshot(const std::filesystem::path& outputFile) {
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);

    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);

    constexpr auto key = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    const auto product = readRegString(HKEY_LOCAL_MACHINE, key, L"ProductName");
    const auto displayVersion = readRegString(HKEY_LOCAL_MACHINE, key, L"DisplayVersion");
    const auto build = readRegString(HKEY_LOCAL_MACHINE, key, L"CurrentBuildNumber");
    const auto gpus = displayAdapters();
    const auto cpu = cpuModel();
    const auto nvidiaDriver = nvidiaDriverVersion();
    const auto displays = activeDisplaysJson();

    const int virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    std::wofstream out(outputFile, std::ios::out | std::ios::trunc);
    if (!out) return false;

    out << L"{\n";
    out << L"  \"windows\": \"" << jsonEscape(product) << L"\",\n";
    out << L"  \"display_version\": \"" << jsonEscape(displayVersion) << L"\",\n";
    out << L"  \"build\": \"" << jsonEscape(build) << L"\",\n";
    out << L"  \"cpu\": \"" << jsonEscape(cpu) << L"\",\n";
    out << L"  \"ram_gb\": " << (mem.ullTotalPhys / (1024ull * 1024ull * 1024ull)) << L",\n";
    out << L"  \"ram_available_gb\": " << (mem.ullAvailPhys / (1024ull * 1024ull * 1024ull)) << L",\n";
    out << L"  \"memory_load_pct\": " << mem.dwMemoryLoad << L",\n";
    out << L"  \"nvidia_driver\": \"" << jsonEscape(nvidiaDriver) << L"\",\n";
    out << L"  \"logical_processors\": " << si.dwNumberOfProcessors << L",\n";
    out << L"  \"display_adapters\": [";
    for (size_t i = 0; i < gpus.size(); ++i) {
        if (i) out << L", ";
        out << L"\"" << jsonEscape(gpus[i]) << L"\"";
    }
    out << L"],\n";
    out << L"  \"displays\": [";
    for (size_t i = 0; i < displays.size(); ++i) {
        if (i) out << L", ";
        out << displays[i];
    }
    out << L"],\n";
    out << L"  \"virtual_desktop\": {\"width\": "
        << virtualWidth
        << L", \"height\": "
        << virtualHeight
        << L"}\n";
    out << L"}\n";
    return true;
}
