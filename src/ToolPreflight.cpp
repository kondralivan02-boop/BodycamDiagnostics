#include "ToolPreflight.h"
#include <Windows.h>
#include <algorithm>
#include <vector>

namespace {
std::wstring quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

std::wstring widenUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

bool runAndCapture(const std::filesystem::path& exe, const std::wstring& args,
                   std::string& output, DWORD timeoutMs = 5000) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return false;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    std::wstring command = quote(exe.wstring()) + L" " + args;
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(
        exe.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, exe.parent_path().c_str(), &si, &pi);

    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        return false;
    }

    std::string captured;
    char buffer[4096];
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;

    for (;;) {
        DWORD available = 0;
        if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            DWORD read = 0;
            const DWORD want = (std::min)(available, static_cast<DWORD>(sizeof(buffer)));
            if (ReadFile(readPipe, buffer, want, &read, nullptr) && read > 0)
                captured.append(buffer, buffer + read);
        }

        const DWORD wait = WaitForSingleObject(pi.hProcess, 50);
        if (wait == WAIT_OBJECT_0) break;
        if (GetTickCount64() >= deadline) {
            TerminateProcess(pi.hProcess, 3);
            WaitForSingleObject(pi.hProcess, 500);
            break;
        }
    }

    for (;;) {
        DWORD read = 0;
        if (!ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
        captured.append(buffer, buffer + read);
    }

    CloseHandle(readPipe);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    output = std::move(captured);
    return true;
}

bool containsAscii(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}
}

std::filesystem::path resolveRuntimeTool(
    const std::filesystem::path& appDirectory,
    const std::filesystem::path& fileName) {

    const auto preferred = appDirectory / L"Tools" / fileName;
    std::error_code ec;
    if (std::filesystem::is_regular_file(preferred, ec))
        return preferred;

    ec.clear();
    const auto fallback = appDirectory / fileName;
    if (std::filesystem::is_regular_file(fallback, ec))
        return fallback;

    return preferred;
}

ToolPreflightResult runToolPreflight(
    const std::filesystem::path& presentMonExe,
    const std::filesystem::path& ffmpegExe) {

    ToolPreflightResult r;
    std::error_code ec;
    r.presentMonFound = std::filesystem::is_regular_file(presentMonExe, ec);
    ec.clear();
    r.ffmpegFound = std::filesystem::is_regular_file(ffmpegExe, ec);

    std::wstring d;
    d += L"PresentMon: ";
    d += r.presentMonFound ? L"OK" : L"MISSING";
    d += L"\nFFmpeg: ";
    d += r.ffmpegFound ? L"OK" : L"MISSING";

    if (r.ffmpegFound) {
        std::string encoders;
        if (runAndCapture(ffmpegExe, L"-hide_banner -encoders", encoders))
            r.nvencAvailable = containsAscii(encoders, "h264_nvenc");

        std::string filters;
        if (runAndCapture(ffmpegExe, L"-hide_banner -filters", filters))
            r.ddagrabAvailable = containsAscii(filters, "ddagrab");

        d += L"\nNVENC (h264_nvenc): ";
        d += r.nvencAvailable ? L"OK" : L"NOT FOUND";
        d += L"\nDesktop Duplication (ddagrab): ";
        d += r.ddagrabAvailable ? L"OK" : L"NOT FOUND";
    }

    r.details = std::move(d);
    return r;
}
