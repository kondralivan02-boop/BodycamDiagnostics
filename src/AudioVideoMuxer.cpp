#include "AudioVideoMuxer.h"
#include "AtomicOutputFile.h"

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

std::filesystem::path recordingPartDirectory(
    const std::filesystem::path& sessionDirectory, unsigned partNumber) {
    return sessionDirectory / L"video" /
        (L"part_" + std::to_wstring(partNumber));
}

std::filesystem::path recordingPartAudioPath(
    const std::filesystem::path& sessionDirectory, unsigned partNumber) {
    return recordingPartDirectory(sessionDirectory, partNumber) / L"game_audio.wav";
}

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
        exe.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, workingDirectory.c_str(), &si, &pi);

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

bool finalizeVideo(const std::filesystem::path& ffmpegExe,
                   const std::filesystem::path& sessionDirectory,
                   bool includeAudio,
                   unsigned partNumber,
                   std::wstring* error) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(ffmpegExe, ec)) {
        if (error) *error = L"ffmpeg.exe not found for audio/video mux";
        return false;
    }

    const auto partLabel = partNumber ? L"part_" + std::to_wstring(partNumber) : std::wstring{};
    const auto videoDir = partNumber ? recordingPartDirectory(sessionDirectory, partNumber) : sessionDirectory / L"video";
    const auto audioFile = partNumber ? recordingPartAudioPath(sessionDirectory, partNumber) : sessionDirectory / L"game_audio.wav";
    ec.clear();
    const bool audioAvailable = includeAudio && std::filesystem::is_regular_file(audioFile, ec);

    ec.clear();
    if (!std::filesystem::is_directory(videoDir, ec)) {
        if (error) *error = L"Video directory not found";
        return false;
    }

    std::vector<std::filesystem::path> segments;
    for (std::filesystem::directory_iterator it(videoDir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const auto& path = it->path();
        if (path.extension() == L".mkv" && path.filename().wstring().rfind(L"video_", 0) == 0) {
            std::error_code sizeEc;
            const auto bytes = std::filesystem::file_size(path, sizeEc);
            if (!sizeEc && bytes > 0) segments.push_back(path.filename());
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

    const auto prefix = partNumber ? L"video_" + partLabel + L"_" : std::wstring{};
    const auto withAudioOutput = sessionDirectory / (prefix + L"video_with_audio.mkv");
    const auto videoOnlyOutput = sessionDirectory / (prefix + L"video_without_audio.mkv");

    ec.clear(); std::filesystem::remove(withAudioOutput, ec);
    ec.clear(); std::filesystem::remove(videoOnlyOutput, ec);

    const auto muxArgs = [&](bool addAudio, const std::filesystem::path& output) {
        std::wstring args = L"-hide_banner -loglevel error -y -f concat -safe 0 -i " +
            quote(concatFile.wstring()) + L" ";
        if (addAudio) {
            args += L"-i " + quote(audioFile.wstring()) +
                L" -map 0:v:0 -map 1:a:0 -c:v copy -c:a aac -b:a 192k -ar 48000 -af apad -shortest ";
        } else {
            args += L"-map 0:v:0 -c:v copy ";
        }
        return args + quote(output.wstring());
    };

    auto output = audioAvailable ? withAudioOutput : videoOnlyOutput;
    std::wstring muxError;
    bool ok = runProcess(ffmpegExe, muxArgs(audioAvailable, output),
                         sessionDirectory, 180000, &muxError);
    std::wstring fallbackWarning;

    const auto nonEmptyOutput = [](const std::filesystem::path& candidate) {
        std::error_code checkEc;
        if (!std::filesystem::is_regular_file(candidate, checkEc))
            return false;
        const auto size = std::filesystem::file_size(candidate, checkEc);
        return !checkEc && size > 0;
    };

    // FFmpeg can exit successfully without producing a usable output. An
    // empty audio mux must trigger the same video-only recovery as an error.
    if (ok && !nonEmptyOutput(output)) {
        ok = false;
        muxError = L"FFmpeg returned success but final video is missing or empty";
    }

    // Audio is optional. If FFmpeg rejects its WAV, retry with the original
    // video segments intact rather than losing the entire recording interval.
    if (!ok && audioAvailable) {
        ec.clear();
        std::filesystem::remove(withAudioOutput, ec);
        if (ec) {
            std::filesystem::remove(concatFile, ec);
            if (error) *error = L"Audio mux failed: " + muxError +
                L"; cannot remove incomplete audio output";
            return false;
        }

        output = videoOnlyOutput;
        std::wstring fallbackError;
        ok = runProcess(ffmpegExe, muxArgs(false, output),
                        sessionDirectory, 180000, &fallbackError);
        if (ok) {
            fallbackWarning = L"Audio mux failed (" + muxError +
                L"); video saved without audio";
        } else {
            muxError += L"; video-only fallback failed: " + fallbackError;
        }
    }

    ec.clear();
    std::filesystem::remove(concatFile, ec);
    if (!ok) {
        // Neither output may be mistaken for a valid completed recording.
        ec.clear();
        std::filesystem::remove(output, ec);
        if (error) *error = muxError;
        return false; // Keep all raw segments and the capture log for recovery.
    }

    if (!nonEmptyOutput(output)) {
        if (error) *error = L"Finalized video is missing or empty: " + output.filename().wstring();
        return false; // Preserve raw segments for recovery.
    }

    std::wstring cleanupWarning = std::move(fallbackWarning);
    const auto captureLog = videoDir / L"video_capture.log";
    const auto preservedCaptureLog = sessionDirectory /
        (partNumber ? L"video_" + partLabel + L"_capture.log" : std::wstring(L"video_capture.log"));
    ec.clear();
    bool captureLogPreserved = true;
    if (std::filesystem::is_regular_file(captureLog, ec)) {
        std::wstring preserveError;
        if (!atomicCopyFile(captureLog, preservedCaptureLog, &preserveError)) {
            captureLogPreserved = false;
            if (!cleanupWarning.empty()) cleanupWarning += L"; ";
            cleanupWarning += L"Could not preserve video_capture.log";
            if (!preserveError.empty()) cleanupWarning += L": " + preserveError;
        }
    }

    // Session-level legacy audio is removed explicitly. Per-part audio lives inside videoDir
    // and is removed together with the successfully finalized raw part below.
    if (!partNumber) {
        ec.clear();
        if (!std::filesystem::remove(audioFile, ec) && ec) {
            if (!cleanupWarning.empty()) cleanupWarning += L"; ";
            cleanupWarning += audioAvailable ? L"Could not remove game_audio.wav" : L"Could not remove incomplete game_audio.wav";
        }
    }

    if (captureLogPreserved) {
        ec.clear();
        std::filesystem::remove_all(videoDir, ec);
        if (ec) {
            if (!cleanupWarning.empty()) cleanupWarning += L"; ";
            cleanupWarning += L"Could not remove raw video directory";
        }
    } else {
        // Never destroy the only remaining FFmpeg capture log after a failed
        // evidence copy, even when the finalized video itself is usable.
        if (!cleanupWarning.empty()) cleanupWarning += L"; ";
        cleanupWarning += L"Raw recording retained to preserve capture log";
    }

    if (error) *error = cleanupWarning;
    return true;
}
}

bool finalizeSessionVideo(const std::filesystem::path& ffmpegExe,
                          const std::filesystem::path& sessionDirectory,
                          bool includeAudio,
                          std::wstring* error) {
    return finalizeVideo(ffmpegExe, sessionDirectory, includeAudio, 0, error);
}

bool finalizeRecordingPart(const std::filesystem::path& ffmpegExe,
                           const std::filesystem::path& sessionDirectory,
                           unsigned partNumber,
                           bool includeAudio,
                           std::wstring* error) {
    if (partNumber == 0) {
        if (error) *error = L"Recording part number must be positive";
        return false;
    }
    return finalizeVideo(ffmpegExe, sessionDirectory, includeAudio, partNumber, error);
}
