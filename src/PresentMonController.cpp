#include "PresentMonController.h"
#include <vector>
#include <chrono>
#include <exception>

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
                                  HANDLE stdOut,
                                  HANDLE stdErr) {
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
        if (!stdErr) nullErr = openNullHandle(GENERIC_WRITE);

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
        si.hStdError = stdErr ? stdErr : nullErr;
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

    const DWORD launchError = ok ? ERROR_SUCCESS : GetLastError();
    if (nullIn && nullIn != INVALID_HANDLE_VALUE)
        CloseHandle(nullIn);
    if (nullErr && nullErr != INVALID_HANDLE_VALUE)
        CloseHandle(nullErr);

    if (!ok) {
        if (error)
            *error = L"CreateProcessW failed: " +
                     std::to_wstring(launchError);
        return false;
    }

    CloseHandle(pi.hThread);
    if (processOut)
        *processOut = pi.hProcess;
    else
        CloseHandle(pi.hProcess);

    return true;
}

bool PresentMonController::beginDiagnostics(
    const std::filesystem::path& csvOutput, HANDLE* writer, std::wstring* error) {
    stderrOutput_.clear();
    stderrReadError_ = ERROR_SUCCESS;
    stopReader_.store(false);
    const auto logPath = std::filesystem::path(csvOutput.wstring() + L".stderr.log");
    stderrLog_ = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (stderrLog_ == INVALID_HANDLE_VALUE) {
        if (error) *error = L"Cannot create bounded PresentMon stderr log: " + std::to_wstring(GetLastError());
        return false;
    }
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    if (!CreatePipe(&stderrRead_, writer, &sa, 4096) ||
        !SetHandleInformation(stderrRead_, HANDLE_FLAG_INHERIT, 0)) {
        const DWORD code = GetLastError();
        if (*writer) { CloseHandle(*writer); *writer = nullptr; }
        finishDiagnostics();
        if (error) *error = L"Cannot prepare PresentMon stderr pipe: " + std::to_wstring(code);
        return false;
    }
    try {
        stderrReader_ = std::thread(&PresentMonController::readDiagnostics, this);
    } catch (const std::exception&) {
        CloseHandle(*writer); *writer = nullptr;
        finishDiagnostics();
        if (error) *error = L"Cannot start PresentMon stderr reader";
        return false;
    }
    return true;
}

void PresentMonController::readDiagnostics() noexcept {
    char buffer[4096];
    // Never wait for pipe EOF: a descendant can inherit the writer. On shutdown
    // drain at most 64 KiB of pending bytes, then close our read end.
    std::size_t shutdownBudget = 64 * 1024;
    for (;;) {
        const bool stopping = stopReader_.load();
        DWORD available = 0;
        if (!PeekNamedPipe(stderrRead_, nullptr, 0, nullptr, &available, nullptr)) {
            const DWORD code = GetLastError();
            if (code != ERROR_BROKEN_PIPE) stderrReadError_ = code;
            break;
        }
        if (!available) {
            if (stopping) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (stopping && !shutdownBudget) break;
        DWORD count = (std::min)(available, static_cast<DWORD>(sizeof(buffer)));
        if (stopping) count = (std::min)(count, static_cast<DWORD>(shutdownBudget));
        DWORD got = 0;
        // Sole reader; bytes reported by Peek cannot be consumed by another
        // thread, so this reads available bytes without waiting for the writer.
        if (!ReadFile(stderrRead_, buffer, count, &got, nullptr) || !got) {
            const DWORD code = GetLastError();
            if (code != ERROR_BROKEN_PIPE) stderrReadError_ = code;
            break;
        }
        stderrOutput_.append(std::string_view(buffer, got));
        if (stopping) shutdownBudget -= got;
    }
}

std::wstring PresentMonController::finishDiagnostics() {
    stopReader_.store(true);
    if (stderrReader_.joinable()) stderrReader_.join();
    if (stderrRead_) { CloseHandle(stderrRead_); stderrRead_ = nullptr; }
    const auto text = stderrOutput_.text();
    DWORD logError = ERROR_SUCCESS;
    if (stderrLog_ != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        if (!WriteFile(stderrLog_, text.data(), static_cast<DWORD>(text.size()), &written, nullptr))
            logError = GetLastError();
        else if (written != text.size()) logError = ERROR_WRITE_FAULT;
        else if (!FlushFileBuffers(stderrLog_)) logError = GetLastError();
        CloseHandle(stderrLog_); stderrLog_ = INVALID_HANDLE_VALUE;
    }
    const auto display = stderrOutput_.text(4096);
    const int length = MultiByteToWideChar(CP_UTF8, 0, display.data(), static_cast<int>(display.size()), nullptr, 0);
    std::wstring result(length > 0 ? static_cast<std::size_t>(length) : 0, L' ');
    if (length > 0) MultiByteToWideChar(CP_UTF8, 0, display.data(), static_cast<int>(display.size()), result.data(), length);
    for (auto& c : result) if (c < 32 || (c >= 127 && c <= 159)) c = L' ';
    result = result.empty() ? L" | stderr: (no output)" : L" | stderr: " + result;
    if (stderrReadError_ != ERROR_SUCCESS) result += L" | stderr read failed: " + std::to_wstring(stderrReadError_);
    if (logError != ERROR_SUCCESS) result += L" | stderr log write failed: " + std::to_wstring(logError);
    return result;
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

    HANDLE staleSessionStopper = nullptr;
    if (launch(
            L"--terminate_existing_session --session_name " +
                std::wstring(kSessionName),
            &staleSessionStopper,
            error)) {

        const DWORD stopWait =
            WaitForSingleObject(staleSessionStopper, 2500);

        if (stopWait != WAIT_OBJECT_0) {
            TerminateProcess(staleSessionStopper, 4);
            WaitForSingleObject(staleSessionStopper, 500);
            CloseHandle(staleSessionStopper);

            if (error)
                *error = L"Timed out while terminating previous PresentMon session";
            return false;
        }

        CloseHandle(staleSessionStopper);
    } else {
        return false;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stderrWriter = nullptr;
    if (!beginDiagnostics(csvOutput, &stderrWriter, error)) return false;

    HANDLE csvHandle = CreateFileW(
        csvOutput.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        &sa,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (csvHandle == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        CloseHandle(stderrWriter);
        const auto details = finishDiagnostics();
        if (error) *error = L"Cannot create PresentMon CSV: " + std::to_wstring(code) + details;
        return false;
    }

    std::wstring args =
        L"--process_id " + std::to_wstring(targetPid) +
        L" --output_stdout" +
        L" --v2_metrics --date_time --no_console_stats" +
        L" --session_name " + std::wstring(kSessionName) +
        L" --terminate_on_proc_exit";

    const bool started =
        launch(args, &process_, error, csvHandle, stderrWriter);
    CloseHandle(stderrWriter);

    CloseHandle(csvHandle);

    if (!started) {
        const auto details = finishDiagnostics();
        if (error) *error += details;
        return false;
    }

    const DWORD startupWait =
        WaitForSingleObject(process_, 500);

    if (startupWait == WAIT_OBJECT_0) {
        DWORD exitCode = 0;
        GetExitCodeProcess(process_, &exitCode);
        CloseHandle(process_);
        process_ = nullptr;

        const auto details = finishDiagnostics();
        if (error) {
            *error =
                L"PresentMon exited during startup with code " +
                std::to_wstring(exitCode) + details;
        }
        return false;
    }

    if (startupWait == WAIT_FAILED) {
        const DWORD waitError = GetLastError();
        TerminateProcess(process_, 5);
        WaitForSingleObject(process_, 500);
        CloseHandle(process_);
        process_ = nullptr;

        const auto details = finishDiagnostics();
        if (error) {
            *error =
                L"PresentMon startup liveness check failed: " +
                std::to_wstring(waitError) + details;
        }
        return false;
    }

    return true;
}

bool PresentMonController::healthy(std::wstring* event) {
    if (!process_) {
        if (event)
            *event = L"PresentMon process handle is unavailable";
        return false;
    }

    const DWORD wait = WaitForSingleObject(process_, 0);
    if (wait == WAIT_TIMEOUT)
        return true;

    if (wait == WAIT_OBJECT_0) {
        DWORD exitCode = 0;
        GetExitCodeProcess(process_, &exitCode);
        CloseHandle(process_);
        process_ = nullptr;

        const auto details = finishDiagnostics();
        if (event) {
            *event =
                L"PresentMon exited during capture with code " +
                std::to_wstring(exitCode) + details;
        }
        return false;
    }

    const DWORD waitError = GetLastError();
    CloseHandle(process_);
    process_ = nullptr;

    const auto details = finishDiagnostics();
    if (event) {
        *event =
            L"PresentMon capture health check failed: " +
            std::to_wstring(waitError) + details;
    }
    return false;
}

void PresentMonController::stop() {
    if (!process_) {
        if (stderrReader_.joinable() || stderrRead_ || stderrLog_ != INVALID_HANDLE_VALUE) finishDiagnostics();
        return;
    }

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
    finishDiagnostics();
}
