#include "RecorderController.h"
#include <vector>

namespace {
std::wstring quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}
}

RecorderController::RecorderController(std::filesystem::path ffmpegExecutable)
    : ffmpegExecutable_(std::move(ffmpegExecutable)) {}

RecorderController::~RecorderController() {
    stop();
}

bool RecorderController::available() const {
    std::error_code ec;
    return std::filesystem::is_regular_file(ffmpegExecutable_, ec);
}

bool RecorderController::start(const std::filesystem::path& outputDirectory, std::wstring* error) {
    if (running()) return true;
    if (!available()) {
        if (error) *error = L"ffmpeg.exe not found: " + ffmpegExecutable_.wstring();
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(outputDirectory, ec);
    if (ec) {
        if (error) *error = L"Cannot create video directory: " + std::to_wstring(ec.value());
        return false;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdinRead = nullptr;
    if (!CreatePipe(&stdinRead, &stdinWrite_, &sa, 0)) {
        if (error) *error = L"CreatePipe failed: " + std::to_wstring(GetLastError());
        stdinWrite_ = nullptr;
        return false;
    }
    SetHandleInformation(stdinWrite_, HANDLE_FLAG_INHERIT, 0);

    const auto outputPattern = (outputDirectory / L"video_%05d.mkv").wstring();
    std::wstring args =
        L" -hide_banner -loglevel warning -y"
        L" -f lavfi -i \"ddagrab=framerate=60:draw_mouse=0\""
        L" -c:v h264_nvenc -cq 18"
        L" -f segment -segment_time 15 -reset_timestamps 1 " + quote(outputPattern);

    std::wstring command = quote(ffmpegExecutable_.wstring()) + args;
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdinRead;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(
        ffmpegExecutable_.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, ffmpegExecutable_.parent_path().c_str(), &si, &pi);

    CloseHandle(stdinRead);

    if (!ok) {
        if (error) *error = L"CreateProcessW(ffmpeg) failed: " + std::to_wstring(GetLastError());
        CloseHandle(stdinWrite_);
        stdinWrite_ = nullptr;
        return false;
    }

    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    return true;
}

void RecorderController::stop() {
    if (!process_) return;

    if (stdinWrite_) {
        const char quit[] = "q\n";
        DWORD written = 0;
        WriteFile(stdinWrite_, quit, sizeof(quit) - 1, &written, nullptr);
        CloseHandle(stdinWrite_);
        stdinWrite_ = nullptr;
    }

    const DWORD wait = WaitForSingleObject(process_, 5000);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(process_, 2);
        WaitForSingleObject(process_, 1000);
    }
    CloseHandle(process_);
    process_ = nullptr;
}
