#include "ReportPackager.h"
#include <Windows.h>

#include <array>
#include <vector>

namespace {
std::wstring quote(const std::wstring& value) {
    std::wstring out = L"\"";
    for (const wchar_t c : value) {
        if (c == L'"') out += L"\\\"";
        else out += c;
    }
    out += L"\"";
    return out;
}

bool runCommand(const std::wstring& command,
                const std::filesystem::path& workingDirectory,
                DWORD timeoutMs,
                DWORD* exitCode,
                std::wstring* error) {
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    const BOOL ok = CreateProcessW(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        workingDirectory.c_str(),
        &si,
        &pi);

    if (!ok) {
        if (error) *error = L"CreateProcessW failed: " + std::to_wstring(GetLastError());
        return false;
    }

    CloseHandle(pi.hThread);
    const DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 4);
        WaitForSingleObject(pi.hProcess, 1000);
        CloseHandle(pi.hProcess);
        if (error) *error = L"Report packaging timed out";
        return false;
    }

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (exitCode) *exitCode = code;

    if (code != 0) {
        if (error) *error = L"tar.exe exited with code " + std::to_wstring(code);
        return false;
    }
    return true;
}
}

bool packageSessionReport(const std::filesystem::path& sessionDirectory,
                          const std::filesystem::path& outputZip,
                          std::wstring* error) {
    static constexpr std::array<const wchar_t*, 13> kReportFiles = {
        L"session.log",
        L"session.json",
        L"system.json",
        L"presentmon.csv",
        L"telemetry.csv",
        L"summary.json",
        L"summary.txt",
        L"validation.json",
        L"validation.txt",
        L"performance_health.json",
        L"performance_health.txt",
        L"stutter_events.json",
        L"stutter_events.txt"
    };

    std::error_code ec;
    if (!std::filesystem::is_directory(sessionDirectory, ec)) {
        if (error) *error = L"Session directory does not exist";
        return false;
    }

    std::vector<std::wstring> existing;
    for (const auto* name : kReportFiles) {
        ec.clear();
        if (std::filesystem::is_regular_file(sessionDirectory / name, ec))
            existing.emplace_back(name);
    }

    std::error_code crashEc;
    if (std::filesystem::is_directory(
            sessionDirectory / L"crash_diagnostics",
            crashEc)) {
        existing.emplace_back(L"crash_diagnostics");
    }

    if (existing.empty()) {
        if (error) *error = L"No report files are available to package";
        return false;
    }

    ec.clear();
    std::filesystem::remove(outputZip, ec);

    std::wstring command = L"tar.exe -a -cf " + quote(std::filesystem::absolute(outputZip).wstring());
    for (const auto& name : existing)
        command += L" " + quote(name);

    DWORD exitCode = 0;
    if (!runCommand(command, sessionDirectory, 30000, &exitCode, error))
        return false;

    ec.clear();
    if (!std::filesystem::is_regular_file(outputZip, ec) || std::filesystem::file_size(outputZip, ec) == 0) {
        if (error) *error = L"report_bundle.zip was not created";
        return false;
    }

    return true;
}
