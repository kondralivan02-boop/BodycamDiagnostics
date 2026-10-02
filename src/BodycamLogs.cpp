#include "BodycamLogs.h"
#include "AtomicOutputFile.h"
#include <algorithm>
#include <cerrno>
#ifdef _WIN32
#include <Windows.h>
#endif
#include <cctype>
#include <fstream>
#include <system_error>

namespace {
std::error_code readTail(const std::filesystem::path& source, std::uintmax_t offset,
                         std::uintmax_t take, std::string& bytes) {
    bytes.assign(static_cast<std::size_t>(take), '\0');
#ifdef _WIN32
    HANDLE file = CreateFileW(source.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {static_cast<int>(GetLastError()), std::system_category()};
    BY_HANDLE_FILE_INFORMATION info{};
    DWORD error = ERROR_SUCCESS;
    if (!GetFileInformationByHandle(file, &info)) error = GetLastError();
    else if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) error = ERROR_NOT_SUPPORTED;
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!error && !SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) error = GetLastError();
    DWORD read = 0;
    if (!error && !ReadFile(file, bytes.data(), static_cast<DWORD>(take), &read, nullptr)) error = GetLastError();
    CloseHandle(file);
    bytes.resize(read);
    if (!error && !read) error = ERROR_HANDLE_EOF;
    return {static_cast<int>(error), std::system_category()};
#else
    errno = 0;
    std::ifstream input(source, std::ios::binary);
    if (!input) return {errno ? errno : EIO, std::generic_category()};
    input.seekg(static_cast<std::streamoff>(offset));
    input.read(bytes.data(), static_cast<std::streamsize>(take));
    const auto read = input.gcount();
    bytes.resize(static_cast<std::size_t>(std::max<std::streamsize>(read, 0)));
    if (input.bad() || read <= 0) return {errno ? errno : EIO, std::generic_category()};
    return {};
#endif
}
std::string utf8Path(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
void recordError(BodycamLogCollection& result, const std::error_code& error) {
    result.source.errorCode = static_cast<std::uint32_t>(error.value());

    bool accessDenied = false;
#ifdef _WIN32
    accessDenied =
        error.category() == std::system_category() &&
        error.value() == ERROR_ACCESS_DENIED;
#else
    accessDenied = error == std::errc::permission_denied;
#endif

    result.source.status = result.files.empty()
        ? (accessDenied ? "access_denied" : "error")
        : "partial";
}
}
BodycamLogCollection collectBodycamLogs(const std::filesystem::path& sourceDirectory,
                                      const std::filesystem::path& sessionDirectory,
                                      std::chrono::system_clock::time_point sessionStart) {
    BodycamLogCollection result;
    if (sourceDirectory.empty()) { result.source.status = "not_configured"; return result; }
    std::error_code ec;
    const auto rootStatus = std::filesystem::symlink_status(sourceDirectory, ec);
    if (ec == std::errc::no_such_file_or_directory || (!ec && !std::filesystem::exists(rootStatus))) {
        result.source.status = "not_found"; return result;
    }
    if (ec) { recordError(result, ec); return result; }
    if (!std::filesystem::is_directory(rootStatus) || std::filesystem::is_symlink(rootStatus)) {
        result.source.status = "unsupported_path"; return result;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    constexpr std::uintmax_t kFileLimit = 2 * 1024 * 1024;
    constexpr std::uintmax_t kTotalLimit = 8 * 1024 * 1024;
    const auto threshold = std::filesystem::file_time_type::clock::now() +
        std::chrono::duration_cast<std::filesystem::file_time_type::duration>(sessionStart - std::chrono::system_clock::now() - std::chrono::minutes(2));
    result.source.status = "available_empty";
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> candidates;
    std::size_t scanned = 0;
    for (std::filesystem::directory_iterator it(sourceDirectory, ec), end; !ec && it != end; it.increment(ec)) {
        if (++scanned > 256 || std::chrono::steady_clock::now() >= deadline) {
            result.source.status = "partial"; result.source.detail += "; enumeration budget reached"; break;
        }
        const auto status = it->symlink_status(ec);
        if (ec) break;
        if (!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) continue;
        std::string extension = utf8Path(it->path().extension());
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".log") continue;
        const auto modified = it->last_write_time(ec);
        if (ec) break;
        if (modified >= threshold) candidates.emplace_back(modified, it->path());
    }
    if (ec) recordError(result, ec);
    if (!candidates.empty() && result.source.status != "available_empty") result.source.status = "partial";
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& candidate : candidates) {
        if (result.files.size() >= 8 || result.bytesCopied >= kTotalLimit || std::chrono::steady_clock::now() >= deadline) {
            result.source.status = "partial"; result.source.detail += "; collection budget reached"; break;
        }
        const auto& source = candidate.second;
        // Recheck metadata; never follow known symbolic links in evidence directories.
        const auto status = std::filesystem::symlink_status(source, ec);
        if (ec || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
            result.source.status = "partial"; continue;
        }
        const auto size = std::filesystem::file_size(source, ec);
        if (ec) { result.source.errorCode = static_cast<std::uint32_t>(ec.value()); result.source.status = "partial"; continue; }
        if (!size) continue;
        const auto take = std::min({size, kFileLimit, kTotalLimit - result.bytesCopied});
        std::string bytes;
        const auto readError = readTail(source, size - take, take, bytes);
        if (readError) {
            recordError(result, readError);
            result.source.detail += "; source read failed";
            continue;
        }
        const auto read = bytes.size();
        std::filesystem::create_directories(sessionDirectory / "bodycam_logs", ec);
        if (ec) { result.source.status = "write_error"; result.source.errorCode = static_cast<std::uint32_t>(ec.value()); break; }
        const auto destination = sessionDirectory / "bodycam_logs" / source.filename();
        AtomicOutputFile outputFile(destination);
        auto& output = outputFile.stream();
        if (!outputFile.isOpen()) {
            result.source.status = "write_error";
            break;
        }
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!output || !outputFile.commit()) {
            result.source.status = "write_error";
            break;
        }
        const bool truncated = static_cast<std::uintmax_t>(read) != size;
        result.files.push_back({utf8Path(source.filename()), size, static_cast<std::uintmax_t>(read), truncated});
        result.bytesCopied += static_cast<std::uintmax_t>(read);
        if (truncated || (result.source.status != "available_empty" && result.source.status != "available")) result.source.status = "partial";
        else if (result.source.status == "available_empty") result.source.status = "available";
    }
    return result;
}
bool writeBodycamLogsManifest(const BodycamLogCollection& result, const std::filesystem::path& output) {
    AtomicOutputFile outputFile(output);
    auto& out = outputFile.stream();
    if (!outputFile.isOpen())
        return false;
    out << "{\"schema_version\":1,\"note\":\"Recent raw logs from known Bodycam/Saved/Logs path only. Last-write time selects files; entries may predate the session. Truncated files retain the tail and may begin mid-line or mid-character. No line-level time correlation.\",\"capabilities\":";
    writeCapabilitiesJson(out, EnvironmentCapabilities{{result.source}});
    out << ",\"bytes_copied\":" << result.bytesCopied << ",\"files\":[";
    bool first = true;
    for (const auto& file : result.files) {
        if (!first) out << ',';
        first = false;
        out << "{\"filename\":" << jsonQuoted(file.filename) << ",\"original_bytes\":" << file.originalBytes
            << ",\"copied_bytes\":" << file.copiedBytes << ",\"tail_only\":" << (file.tailOnly ? "true" : "false") << '}';
    }
    out << "]}\n";
    if (!out)
        return false;
    return outputFile.commit();
}
