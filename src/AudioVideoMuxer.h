#pragma once

#include <filesystem>
#include <string>

// Share the per-interval WAV location between recording and muxing.
std::filesystem::path recordingPartDirectory(
    const std::filesystem::path& sessionDirectory, unsigned partNumber);
std::filesystem::path recordingPartAudioPath(
    const std::filesystem::path& sessionDirectory, unsigned partNumber);

bool finalizeSessionVideo(
    const std::filesystem::path& ffmpegExe,
    const std::filesystem::path& sessionDirectory,
    bool includeAudio,
    std::wstring* error = nullptr);

bool finalizeRecordingPart(
    const std::filesystem::path& ffmpegExe,
    const std::filesystem::path& sessionDirectory,
    unsigned partNumber,
    bool includeAudio,
    std::wstring* error = nullptr);
