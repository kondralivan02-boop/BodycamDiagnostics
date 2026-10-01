#pragma once

#include <chrono>
#include <filesystem>
#include <string>

struct CrashCollectionResult {
    bool copiedAnything = false;
    std::size_t filesCopied = 0;
    std::uintmax_t bytesCopied = 0;
    std::wstring details;
};

CrashCollectionResult collectRecentBodycamCrashData(
    const std::filesystem::path& sessionDirectory,
    std::chrono::system_clock::time_point sessionStartWallClock);
