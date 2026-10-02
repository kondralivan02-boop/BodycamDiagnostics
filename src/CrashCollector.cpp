#include "CrashCollector.h"
#include "AtomicOutputFile.h"

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <vector>

namespace {
std::filesystem::path localAppData() {
    wchar_t buffer[32768]{};
    const DWORD n = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        buffer,
        static_cast<DWORD>(std::size(buffer)));

    if (n == 0 || n >= std::size(buffer))
        return {};

    return std::filesystem::path(buffer);
}

bool recentEnough(const std::filesystem::path& path,
                  std::chrono::system_clock::time_point threshold) {
    std::error_code ec;
    const auto ft = std::filesystem::last_write_time(path, ec);
    if (ec)
        return false;

    const auto nowFile = std::filesystem::file_time_type::clock::now();
    const auto nowSys = std::chrono::system_clock::now();
    const auto sysTime =
        nowSys + std::chrono::duration_cast<std::chrono::system_clock::duration>(
            ft - nowFile);

    return sysTime >= threshold;
}

bool copyOne(const std::filesystem::path& source,
             const std::filesystem::path& target,
             std::uintmax_t maxBytes,
             std::uintmax_t& copiedBytes) {
    std::error_code ec;

    if (!std::filesystem::is_regular_file(source, ec))
        return false;

    const auto size = std::filesystem::file_size(source, ec);
    if (ec || size == 0 || size > maxBytes)
        return false;

    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec)
        return false;

    if (!atomicCopyFile(source, target))
        return false;

    copiedBytes += size;
    return true;
}

void copyRecentTree(const std::filesystem::path& sourceRoot,
                    const std::filesystem::path& targetRoot,
                    std::chrono::system_clock::time_point threshold,
                    CrashCollectionResult& result) {
    std::error_code ec;
    if (!std::filesystem::is_directory(sourceRoot, ec))
        return;

    constexpr std::uintmax_t kMaxSingleFile = 32ull * 1024ull * 1024ull;
    constexpr std::uintmax_t kMaxTotal = 96ull * 1024ull * 1024ull;

    for (std::filesystem::recursive_directory_iterator it(
             sourceRoot,
             std::filesystem::directory_options::skip_permission_denied,
             ec),
         end;
         !ec && it != end;
         it.increment(ec)) {

        if (result.bytesCopied >= kMaxTotal)
            break;

        if (!it->is_regular_file(ec))
            continue;

        const auto source = it->path();
        if (!recentEnough(source, threshold))
            continue;

        auto relative = std::filesystem::relative(source, sourceRoot, ec);
        if (ec) {
            ec.clear();
            relative = source.filename();
        }

        if (copyOne(
                source,
                targetRoot / relative,
                kMaxSingleFile,
                result.bytesCopied)) {
            ++result.filesCopied;
            result.copiedAnything = true;
        }
    }
}
}

CrashCollectionResult collectRecentBodycamCrashData(
    const std::filesystem::path& sessionDirectory,
    std::chrono::system_clock::time_point sessionStartWallClock) {

    CrashCollectionResult result;
    const auto base = localAppData();

    if (base.empty()) {
        result.details = L"LOCALAPPDATA is unavailable";
        return result;
    }

    const auto threshold =
        sessionStartWallClock - std::chrono::minutes(2);

    const auto target = sessionDirectory / L"crash_diagnostics";

    copyRecentTree(
        base / L"Bodycam" / L"Saved" / L"Crashes",
        target / L"Crashes",
        threshold,
        result);

    copyRecentTree(
        base / L"Bodycam" / L"Saved" / L"Logs",
        target / L"Logs",
        threshold,
        result);

    if (result.copiedAnything) {
        result.details =
            std::to_wstring(result.filesCopied) +
            L" file(s), " +
            std::to_wstring(result.bytesCopied / 1024ull) +
            L" KiB";
    } else {
        result.details =
            L"No recent Bodycam crash/log files found in LOCALAPPDATA";
    }

    return result;
}
