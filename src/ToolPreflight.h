#pragma once
#include <filesystem>
#include <string>

struct ToolPreflightResult {
    bool presentMonFound = false;
    bool ffmpegFound = false;
    bool nvencAvailable = false;
    bool ddagrabAvailable = false;
    std::wstring details;

    bool recordingReady() const noexcept {
        return ffmpegFound && nvencAvailable && ddagrabAvailable;
    }
};

std::filesystem::path resolveRuntimeTool(
    const std::filesystem::path& appDirectory,
    const std::filesystem::path& fileName);

ToolPreflightResult runToolPreflight(
    const std::filesystem::path& presentMonExe,
    const std::filesystem::path& ffmpegExe);
