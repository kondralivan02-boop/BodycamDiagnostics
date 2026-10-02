#include "AudioVideoMuxer.h"
#include "AtomicOutputFile.h"

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

namespace {
std::wstring quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

bool runProcess(const std::filesystem::path& exe,
                const std::wstring& args,
                const std::filesystem::path& workingDirectory,
                DWORD timeoutMs,
                std::wstring* error) {
    std::wstring command = quote(exe.wstring()) + L" " + args;
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    const BOOL ok = CreateProcessW(
        exe.c_str(),
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
        if (error) *error = L"CreateProcessW(ffmpeg mux) failed: " + std::to_wstring(GetLastError());
        return false;
    }

    CloseHandle(pi.hThread);

    const DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 5);
        WaitForSingleObject(pi.hProcess, 1000);
        CloseHandle(pi.hProcess);
        if (error) *error = L"Audio/video mux timed out";
        return false;
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);

    if (exitCode != 0) {
        if (error) *error = L"ffmpeg mux exited with code " + std::to_wstring(exitCode);
        return false;
    }

    return true;
}
}

bool finalizeSessionVideo(
    const std::filesystem::path& ffmpegExe,
    const std::filesystem::path& sessionDirectory,
    bool includeAudio,
    std::wstring* error) {
    std::error_code ec;

    if (!std::filesystem::is_regular_file(ffmpegExe, ec)) {
        if (error) *error = L"ffmpeg.exe not found for audio/video mux";
        return false;
    }

    const auto audioFile = sessionDirectory / L"game_audio.wav";
    ec.clear();
    const bool audioAvailable =
        includeAudio &&
        std::filesystem::is_regular_file(audioFile, ec);

    const auto videoDir = sessionDirectory / L"video";
    ec.clear();
    if (!std::filesystem::is_directory(videoDir, ec)) {
        if (error) *error = L"Video directory not found";
        return false;
    }

    std::vector<std::filesystem::path> segments;
    for (std::filesystem::directory_iterator it(videoDir, ec), end;
         !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const auto& path = it->path();
        if (path.extension() == L".mkv" &&
            path.filename().wstring().rfind(L"video_", 0) == 0) {
            std::error_code sizeEc;
            const auto bytes = std::filesystem::file_size(path, sizeEc);
            if (!sizeEc && bytes > 0)
                segments.push_back(path.filename());
        }
    }

    if (segments.empty()) {
        if (error) *error = L"No video segments found";
        return false;
    }

    std::sort(segments.begin(), segments.end());

    const auto concatFile = videoDir / L"video_concat.txt";
    {
        std::ofstream out(concatFile, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = L"Cannot create video_concat.txt";
            return false;
        }

        for (const auto& segment : segments) {
            const auto name = segment.u8string();
            out << "file '" << reinterpret_cast<const char*>(name.c_str()) << "'\n";
        }
    }

    const auto output = sessionDirectory /
        (audioAvailable
            ? L"video_with_audio.mkv"
            : L"video_without_audio.mkv");

    const auto alternateOutput = sessionDirectory /
        (audioAvailable
            ? L"video_without_audio.mkv"
            : L"video_with_audio.mkv");

    ec.clear();
    std::filesystem::remove(output, ec);
    ec.clear();
    std::filesystem::remove(alternateOutput, ec);

    std::wstring args =
        L"-hide_banner -loglevel error -y "
        L"-f concat -safe 0 -i " + quote(concatFile.wstring()) + L" ";

    if (audioAvailable) {
        args +=
            L"-i " + quote(audioFile.wstring()) + L" "
            L"-map 0:v:0 -map 1:a:0 "
            L"-c:v copy -c:a aac -b:a 192k -ar 48000 "
            L"-af apad -shortest ";
    } else {
        args +=
            L"-map 0:v:0 -c:v copy ";
    }

    args += quote(output.wstring());

    const bool ok = runProcess(
        ffmpegExe,
        args,
        sessionDirectory,
        180000,
        error);

    std::filesystem::remove(concatFile, ec);

    if (!ok) return false;

    ec.clear();
    if (!std::filesystem::is_regular_file(output, ec) ||
        std::filesystem::file_size(output, ec) == 0) {
        if (error) {
            *error =
                audioAvailable
                    ? L"video_with_audio.mkv was not created"
                    : L"video_without_audio.mkv was not created";
        }
        return false;
    }

    // Final file is valid: preserve diagnostic evidence before removing raw capture files.
    // If muxing fails above, the sources are intentionally kept for recovery.
    std::wstring cleanupWarning;

    const auto captureLog = videoDir / L"video_capture.log";
    const auto preservedCaptureLog = sessionDirectory / L"video_capture.log";
    ec.clear();
    if (std::filesystem::is_regular_file(captureLog, ec)) {
        std::wstring preserveError;
        if (!atomicCopyFile(
                captureLog,
                preservedCaptureLog,
                &preserveError)) {
            cleanupWarning +=
                L"Could not preserve video_capture.log";
            if (!preserveError.empty())
                cleanupWarning += L": " + preserveError;
        }
    }

    ec.clear();
    if (!std::filesystem::remove(audioFile, ec) && ec) {
        if (!cleanupWarning.empty())
            cleanupWarning += L"; ";
        cleanupWarning +=
            audioAvailable
                ? L"Could not remove game_audio.wav"
                : L"Could not remove incomplete game_audio.wav";
    }

    ec.clear();
    std::filesystem::remove_all(videoDir, ec);
    if (ec) {
        if (!cleanupWarning.empty())
            cleanupWarning += L"; ";
        cleanupWarning += L"Could not remove raw video directory";
    }

    if (error)
        *error = cleanupWarning;

    return true;
}
