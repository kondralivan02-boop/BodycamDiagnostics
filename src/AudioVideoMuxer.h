#pragma once

#include <filesystem>
#include <string>

bool finalizeSessionVideo(
    const std::filesystem::path& ffmpegExe,
    const std::filesystem::path& sessionDirectory,
    bool includeAudio,
    std::wstring* error = nullptr);
