#include "SystemSnapshot.h"
#include "AtomicOutputFile.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <sstream>

namespace {
struct DirectoryProbe {
    bool requested = false;
    bool pathResolved = false;
    bool exists = false;
    bool isDirectory = false;
    bool readable = false;
    DWORD error = ERROR_SUCCESS;
};

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

const wchar_t* boolJson(bool value) {
    return value ? L"true" : L"false";
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

std::wstring environmentVariable(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0)
        return {};

    std::wstring value(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
    if (written == 0 || written >= required)
        return {};

    value.resize(written);
    return value;
}

DirectoryProbe probeDirectory(const std::filesystem::path& path) {
    DirectoryProbe probe{};
    probe.requested = true;
    probe.pathResolved = !path.empty();

    if (!probe.pathResolved) {
        probe.error = ERROR_ENVVAR_NOT_FOUND;
        return probe;
    }

    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        probe.error = GetLastError();
        return probe;
    }

    probe.exists = true;
    probe.isDirectory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    if (!probe.isDirectory) {
        probe.error = ERROR_DIRECTORY;
        return probe;
    }

    const auto wildcard = (path / L"*").wstring();
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(wildcard.c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        FindClose(find);
        probe.readable = true;
        probe.error = ERROR_SUCCESS;
        return probe;
    }

    probe.error = GetLastError();

    if (probe.error == ERROR_FILE_NOT_FOUND) {
        probe.readable = true;
        probe.error = ERROR_SUCCESS;
    }

    return probe;
}

std::wstring bodycamLogStatus(const DirectoryProbe& probe) {
    if (!probe.pathResolved)
        return L"localappdata_unresolved";
    if (!probe.exists)
        return probe.error == ERROR_ACCESS_DENIED
            ? L"access_denied"
            : L"missing";
    if (!probe.isDirectory)
        return L"not_directory";
    if (!probe.readable)
        return probe.error == ERROR_ACCESS_DENIED
            ? L"access_denied"
            : L"unreadable";
    return L"available";
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
    const auto localAppData = environmentVariable(L"LOCALAPPDATA");
    const auto bodycamLogs = localAppData.empty()
        ? std::filesystem::path{}
        : std::filesystem::path(localAppData) / L"Bodycam" / L"Saved" / L"Logs";
    const auto bodycamLogProbe = probeDirectory(bodycamLogs);
    const auto systemDriveProbe = probeDirectory(L"C:\\");

    const int virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    AtomicWideOutputFile atomicOut(
        outputFile,
        std::ios::out | std::ios::trunc);
    auto& out = atomicOut.stream();
    if (!atomicOut.isOpen())
        return false;

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
        << L"},\n";
    out << L"  \"restricted_environment\": {\n";
    out << L"    \"system_drive_c_exists\": " << boolJson(systemDriveProbe.exists) << L",\n";
    out << L"    \"system_drive_c_readable\": " << boolJson(systemDriveProbe.readable) << L",\n";
    out << L"    \"system_drive_c_error\": " << systemDriveProbe.error << L",\n";
    out << L"    \"localappdata_resolved\": " << boolJson(!localAppData.empty()) << L",\n";
    out << L"    \"bodycam_logs_checked\": " << boolJson(bodycamLogProbe.requested) << L",\n";
    out << L"    \"bodycam_logs_exists\": " << boolJson(bodycamLogProbe.exists) << L",\n";
    out << L"    \"bodycam_logs_readable\": " << boolJson(bodycamLogProbe.readable) << L",\n";
    out << L"    \"bodycam_logs_error\": " << bodycamLogProbe.error << L",\n";
    out << L"    \"bodycam_logs_status\": \"" << bodycamLogStatus(bodycamLogProbe) << L"\",\n";
    out << L"    \"note\": \"Bodycam logs are optional. Restricted access does not invalidate the capture.\"\n";
    out << L"  }\n";
    out << L"}\n";
    if (!out)
        return false;
    return atomicOut.commit();
}
