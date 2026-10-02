#include "RecorderController.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
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

unsigned RecorderController::nextSegmentNumber() const {
    unsigned next = 0;
    std::error_code ec;

    if (!std::filesystem::is_directory(outputDirectory_, ec))
        return next;

    for (std::filesystem::directory_iterator it(outputDirectory_, ec), end;
         !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec))
            continue;

        const auto name = it->path().filename().wstring();
        if (name.size() != 15 ||
            name.rfind(L"video_", 0) != 0 ||
            name.substr(name.size() - 4) != L".mkv") {
            continue;
        }

        const auto digits = name.substr(6, 5);
        if (!std::all_of(
                digits.begin(), digits.end(),
                [](wchar_t c) { return std::iswdigit(c) != 0; })) {
            continue;
        }

        try {
            next = (std::max)(
                next,
                static_cast<unsigned>(std::stoul(digits)) + 1u);
        } catch (...) {
        }
    }

    return next;
}

void RecorderController::closeChildHandles() {
    if (stdinWrite_) {
        CloseHandle(stdinWrite_);
        stdinWrite_ = nullptr;
    }

    if (process_) {
        CloseHandle(process_);
        process_ = nullptr;
    }
}

bool RecorderController::launchCapture(std::wstring* error) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdinRead = nullptr;
    HANDLE stdinWrite = nullptr;
    if (!CreatePipe(&stdinRead, &stdinWrite, &sa, 0)) {
        if (error)
            *error = L"CreatePipe failed: " +
                     std::to_wstring(GetLastError());
        return false;
    }

    SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);

    const auto logPath = outputDirectory_ / L"video_capture.log";
    HANDLE logHandle = CreateFileW(
        logPath.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (logHandle == INVALID_HANDLE_VALUE) {
        CloseHandle(stdinRead);
        CloseHandle(stdinWrite);
        if (error)
            *error = L"Cannot open video_capture.log: " +
                     std::to_wstring(GetLastError());
        return false;
    }

    const auto outputPattern =
        (outputDirectory_ / L"video_%05d.mkv").wstring();
    const unsigned startNumber = nextSegmentNumber();

    std::wstring args =
        L" -hide_banner -loglevel warning -y"
        L" -f lavfi -i \"ddagrab=framerate=60:draw_mouse=0\""
        L" -c:v h264_nvenc -cq 18"
        L" -f segment -segment_time 15 -reset_timestamps 1"
        L" -segment_start_number " + std::to_wstring(startNumber) +
        L" " + quote(outputPattern);

    std::wstring command = quote(ffmpegExecutable_.wstring()) + args;
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdinRead;
    si.hStdOutput = logHandle;
    si.hStdError = logHandle;

    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(
        ffmpegExecutable_.c_str(),
        mutableCommand.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        ffmpegExecutable_.parent_path().c_str(),
        &si,
        &pi);

    CloseHandle(stdinRead);
    CloseHandle(logHandle);

    if (!ok) {
        CloseHandle(stdinWrite);
        if (error)
            *error = L"CreateProcessW(ffmpeg) failed: " +
                     std::to_wstring(GetLastError());
        return false;
    }

    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    stdinWrite_ = stdinWrite;

    const DWORD startupWait =
        WaitForSingleObject(process_, 500);

    if (startupWait == WAIT_OBJECT_0) {
        DWORD exitCode = 0;
        GetExitCodeProcess(process_, &exitCode);
        closeChildHandles();

        if (error) {
            *error =
                L"FFmpeg exited during startup with code " +
                std::to_wstring(exitCode) +
                L"; see video_capture.log";
        }
        return false;
    }

    if (startupWait == WAIT_FAILED) {
        const DWORD waitError = GetLastError();
        TerminateProcess(process_, 5);
        WaitForSingleObject(process_, 500);
        closeChildHandles();

        if (error) {
            *error =
                L"FFmpeg startup liveness check failed: " +
                std::to_wstring(waitError);
        }
        return false;
    }

    return true;
}

bool RecorderController::start(const std::filesystem::path& outputDirectory,
                               std::wstring* error) {
    if (running())
        return true;

    if (!available()) {
        if (error)
            *error = L"ffmpeg.exe not found: " +
                     ffmpegExecutable_.wstring();
        return false;
    }

    outputDirectory_ = outputDirectory;
    restartCount_ = 0;
    disabled_ = false;

    std::error_code ec;
    std::filesystem::create_directories(outputDirectory_, ec);
    if (ec) {
        if (error)
            *error = L"Cannot create video directory: " +
                     std::to_wstring(ec.value());
        return false;
    }

    return launchCapture(error);
}

bool RecorderController::maintain(std::wstring* event) {
    if (disabled_ || !process_)
        return false;

    if (WaitForSingleObject(process_, 0) == WAIT_TIMEOUT)
        return true;

    DWORD exitCode = 0;
    GetExitCodeProcess(process_, &exitCode);
    closeChildHandles();

    std::ifstream log(outputDirectory_ / L"video_capture.log", std::ios::binary);
    const std::string logText((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
    if (logText.find("887a0026") != std::string::npos || logText.find("0x887A0026") != std::string::npos) {
        disabled_ = true;
        if (event) *event = L"Захват экрана потерян: графическая или удалённая сессия была сброшена (0x887A0026)";
        return false;
    }

    constexpr unsigned kMaxRestarts = 1;
    if (restartCount_ >= kMaxRestarts) {
        disabled_ = true;
        if (event) {
            *event =
                L"NVENC video stopped after repeated FFmpeg exits"
                L"  •  last code " +
                std::to_wstring(exitCode) +
                L"  •  see video_capture.log";
        }
        return false;
    }

    ++restartCount_;

    std::wstring restartError;
    if (!launchCapture(&restartError)) {
        disabled_ = true;
        if (event) {
            *event =
                L"NVENC video restart failed"
                L"  •  " + restartError;
        }
        return false;
    }

    if (event) {
        *event =
            L"NVENC video recovered"
            L"  •  FFmpeg exit code " +
            std::to_wstring(exitCode) +
            L"  •  restart " +
            std::to_wstring(restartCount_);
    }

    return true;
}

void RecorderController::stop() {
    if (!process_) {
        if (stdinWrite_) {
            CloseHandle(stdinWrite_);
            stdinWrite_ = nullptr;
        }
        return;
    }

    if (stdinWrite_) {
        const char quit[] = "q\n";
        DWORD written = 0;
        WriteFile(
            stdinWrite_,
            quit,
            sizeof(quit) - 1,
            &written,
            nullptr);
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
