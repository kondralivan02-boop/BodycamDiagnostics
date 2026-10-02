#pragma once
#include <filesystem>
#include <string>

struct ToolPreflightResult {
    bool presentMonFound = false;
    bool ffmpegFound = false;
    bool presentMonTrusted = false;
    bool ffmpegTrusted = false;
    bool nvencAvailable = false;
    bool ddagrabAvailable = false;
    std::wstring details;

    bool recordingReady() const noexcept {
        return ffmpegFound && ffmpegTrusted && nvencAvailable && ddagrabAvailable;
    }
};

std::string fileSha256Hex(const std::filesystem::path& file);
std::string expectedRuntimeToolSha256(
    const std::filesystem::path& manifest,
    const std::filesystem::path& fileName);

std::filesystem::path resolveRuntimeTool(
    const std::filesystem::path& appDirectory,
    const std::filesystem::path& fileName);

ToolPreflightResult runToolPreflight(
    const std::filesystem::path& presentMonExe,
    const std::filesystem::path& ffmpegExe);
