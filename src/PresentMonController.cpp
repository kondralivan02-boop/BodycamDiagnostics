#include "PresentMonController.h"
#include <vector>

namespace {
std::wstring quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

HANDLE openNullHandle(DWORD access) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    return CreateFileW(
        L"NUL",
        access,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
}
}

PresentMonController::PresentMonController(std::filesystem::path executable)
    : executable_(std::move(executable)) {}

PresentMonController::~PresentMonController() {
    stop();
}

bool PresentMonController::available() const {
    std::error_code ec;
    return std::filesystem::is_regular_file(executable_, ec);
}

bool PresentMonController::launch(const std::wstring& arguments,
                                  HANDLE* processOut,
                                  std::wstring* error,
                                  HANDLE stdOut) {
    std::wstring command = quote(executable_.wstring()) + L" " + arguments;
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);

    HANDLE nullIn = nullptr;
    HANDLE nullErr = nullptr;
    BOOL inheritHandles = FALSE;

    if (stdOut) {
        nullIn = openNullHandle(GENERIC_READ);
        nullErr = openNullHandle(GENERIC_WRITE);

        if (nullIn == INVALID_HANDLE_VALUE ||
            nullErr == INVALID_HANDLE_VALUE) {
            if (nullIn && nullIn != INVALID_HANDLE_VALUE)
                CloseHandle(nullIn);
            if (nullErr && nullErr != INVALID_HANDLE_VALUE)
                CloseHandle(nullErr);

            if (error)
                *error = L"Failed to prepare PresentMon stdout redirection";
            return false;
        }

        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput = nullIn;
        si.hStdOutput = stdOut;
        si.hStdError = nullErr;
        inheritHandles = TRUE;
    }

    PROCESS_INFORMATION pi{};

    const BOOL ok = CreateProcessW(
        executable_.c_str(),
        mutableCommand.data(),
        nullptr,
        nullptr,
        inheritHandles,
        CREATE_NO_WINDOW,
        nullptr,
        executable_.parent_path().c_str(),
        &si,
        &pi);

    if (nullIn && nullIn != INVALID_HANDLE_VALUE)
        CloseHandle(nullIn);
    if (nullErr && nullErr != INVALID_HANDLE_VALUE)
        CloseHandle(nullErr);

    if (!ok) {
        if (error)
            *error = L"CreateProcessW failed: " +
                     std::to_wstring(GetLastError());
        return false;
    }

    CloseHandle(pi.hThread);
    if (processOut)
        *processOut = pi.hProcess;
    else
        CloseHandle(pi.hProcess);

    return true;
}

bool PresentMonController::start(DWORD targetPid,
                                 const std::filesystem::path& csvOutput,
                                 std::wstring* error) {
    if (running()) return true;

    if (!available()) {
        if (error)
            *error = L"PresentMon executable not found: " +
                     executable_.wstring();
        return false;
    }

    launch(
        L"--terminate_existing_session --session_name " +
            std::wstring(kSessionName),
        nullptr,
        nullptr);

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE csvHandle = CreateFileW(
        csvOutput.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        &sa,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (csvHandle == INVALID_HANDLE_VALUE) {
        if (error)
            *error = L"Cannot create PresentMon CSV: " +
                     std::to_wstring(GetLastError());
        return false;
    }

    std::wstring args =
        L"--process_id " + std::to_wstring(targetPid) +
        L" --output_stdout" +
        L" --v2_metrics --date_time --no_console_stats" +
        L" --session_name " + std::wstring(kSessionName) +
        L" --terminate_on_proc_exit";

    const bool started =
        launch(args, &process_, error, csvHandle);

    CloseHandle(csvHandle);
    return started;
}

void PresentMonController::stop() {
    if (!process_) return;

    HANDLE stopper = nullptr;
    launch(
        L"--terminate_existing_session --session_name " +
            std::wstring(kSessionName),
        &stopper,
        nullptr);

    if (stopper) {
        WaitForSingleObject(stopper, 2500);
        CloseHandle(stopper);
    }

    WaitForSingleObject(process_, 2500);
    CloseHandle(process_);
    process_ = nullptr;
}
