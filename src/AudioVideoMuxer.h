#pragma once

#include <filesystem>
#include <string>

bool muxSessionVideoWithAudio(const std::filesystem::path& ffmpegExe,
                              const std::filesystem::path& sessionDirectory,
                              std::wstring* error = nullptr);
