#pragma once

#include <filesystem>
#include <string>

bool writeBuildInfoReport(
    const std::filesystem::path& path,
    std::wstring* error = nullptr);
