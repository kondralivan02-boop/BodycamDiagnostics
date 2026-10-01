#include "PresentMonController.h"
#include <vector>

namespace {
std::wstring quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
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

bool PresentMonController::launch(const std::wstring& arguments, HANDLE* processOut, std::wstring* error) {
    std::wstring command = quote(executable_.wstring()) + L" " + arguments;
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    const BOOL ok = CreateProcessW(
        executable_.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, executable_.parent_path().c_str(), &si, &pi);

    if (!ok) {
        if (error) *error = L"CreateProcessW failed: " + std::to_wstring(GetLastError());
        return false;
    }

    CloseHandle(pi.hThread);
    if (processOut) *processOut = pi.hProcess;
    else CloseHandle(pi.hProcess);
    return true;
}

bool PresentMonController::start(DWORD targetPid, const std::filesystem::path& csvOutput, std::wstring* error) {
    if (running()) return true;
    if (!available()) {
        if (error) *error = L"PresentMon executable not found: " + executable_.wstring();
        return false;
    }

    launch(L"--terminate_existing_session --session_name " + std::wstring(kSessionName), nullptr, nullptr);

    std::wstring args =
        L"--process_id " + std::to_wstring(targetPid) +
        L" --output_file " + quote(csvOutput.wstring()) +
        L" --v2_metrics --date_time --no_console_stats" +
        L" --session_name " + std::wstring(kSessionName) +
        L" --terminate_on_proc_exit";

    return launch(args, &process_, error);
}

void PresentMonController::stop() {
    if (!process_) return;

    HANDLE stopper = nullptr;
    launch(L"--terminate_existing_session --session_name " + std::wstring(kSessionName), &stopper, nullptr);
    if (stopper) {
        WaitForSingleObject(stopper, 2500);
        CloseHandle(stopper);
    }

    WaitForSingleObject(process_, 2500);
    CloseHandle(process_);
    process_ = nullptr;
}
