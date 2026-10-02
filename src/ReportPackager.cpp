#include "ReportPackager.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t kLocalFileHeaderSignature = 0x04034b50u;
constexpr std::uint32_t kCentralDirectorySignature = 0x02014b50u;
constexpr std::uint32_t kEndOfCentralDirectorySignature = 0x06054b50u;
constexpr std::uint16_t kZipVersion20 = 20u;
constexpr std::uint16_t kUtf8Flag = 0x0800u;
constexpr std::uint16_t kStoreMethod = 0u;
constexpr std::uint16_t kDosTime = 0u;
constexpr std::uint16_t kDosDate = 0x0021u; // 1980-01-01

struct ArchiveEntry {
    std::filesystem::path source;
    std::string name;
    std::uint32_t crc32 = 0;
    std::uint32_t size = 0;
    std::uint32_t localHeaderOffset = 0;
};

void writeU16(std::ofstream& out, std::uint16_t value) {
    const std::array<unsigned char, 2> bytes = {
        static_cast<unsigned char>(value & 0xffu),
        static_cast<unsigned char>((value >> 8u) & 0xffu)
    };
    out.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
}

void writeU32(std::ofstream& out, std::uint32_t value) {
    const std::array<unsigned char, 4> bytes = {
        static_cast<unsigned char>(value & 0xffu),
        static_cast<unsigned char>((value >> 8u) & 0xffu),
        static_cast<unsigned char>((value >> 16u) & 0xffu),
        static_cast<unsigned char>((value >> 24u) & 0xffu)
    };
    out.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
}

std::uint32_t updateCrc32(
    std::uint32_t crc,
    const unsigned char* data,
    std::size_t size) {

    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask =
                0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

std::string archiveName(
    const std::filesystem::path& sessionDirectory,
    const std::filesystem::path& file) {

    std::error_code ec;
    const auto relative =
        std::filesystem::relative(file, sessionDirectory, ec);
    if (ec || relative.empty())
        return {};

    const auto utf8 = relative.generic_u8string();
    return std::string(
        reinterpret_cast<const char*>(utf8.data()),
        utf8.size());
}

bool analyzeFile(ArchiveEntry& entry, std::wstring* error) {
    std::ifstream in(entry.source, std::ios::binary);
    if (!in) {
        if (error)
            *error = L"Cannot open report file: " +
                     entry.source.wstring();
        return false;
    }

    std::array<unsigned char, 64 * 1024> buffer{};
    std::uint64_t total = 0;
    std::uint32_t crc = 0;

    for (;;) {
        in.read(
            reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(buffer.size()));

        const auto count = in.gcount();
        if (count > 0) {
            const auto bytes =
                static_cast<std::size_t>(count);
            total += static_cast<std::uint64_t>(bytes);
            if (total >
                static_cast<std::uint64_t>(
                    (std::numeric_limits<std::uint32_t>::max)())) {
                if (error)
                    *error = L"Report file exceeds classic ZIP limit: " +
                             entry.source.wstring();
                return false;
            }

            crc = updateCrc32(crc, buffer.data(), bytes);
        }

        if (in.eof())
            break;

        if (!in) {
            if (error)
                *error = L"Cannot read report file: " +
                         entry.source.wstring();
            return false;
        }
    }

    entry.size = static_cast<std::uint32_t>(total);
    entry.crc32 = crc;
    return true;
}

bool appendRegularFile(
    const std::filesystem::path& sessionDirectory,
    const std::filesystem::path& file,
    std::vector<ArchiveEntry>& entries) {

    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec))
        return true;

    const auto name = archiveName(sessionDirectory, file);
    if (name.empty())
        return false;

    entries.push_back(ArchiveEntry{file, name});
    return true;
}

bool appendDirectory(
    const std::filesystem::path& sessionDirectory,
    const std::filesystem::path& directory,
    std::vector<ArchiveEntry>& entries) {

    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec))
        return true;

    std::filesystem::recursive_directory_iterator it(directory, ec);
    const std::filesystem::recursive_directory_iterator end;

    while (!ec && it != end) {
        std::error_code typeEc;
        if (it->is_regular_file(typeEc)) {
            if (!appendRegularFile(
                    sessionDirectory,
                    it->path(),
                    entries)) {
                return false;
            }
        }
        it.increment(ec);
    }

    return !ec;
}

bool writeFileData(
    std::ofstream& out,
    const ArchiveEntry& entry,
    std::wstring* error) {

    std::ifstream in(entry.source, std::ios::binary);
    if (!in) {
        if (error)
            *error = L"Cannot reopen report file: " +
                     entry.source.wstring();
        return false;
    }

    std::array<char, 64 * 1024> buffer{};
    for (;;) {
        in.read(
            buffer.data(),
            static_cast<std::streamsize>(buffer.size()));
        const auto count = in.gcount();

        if (count > 0)
            out.write(buffer.data(), count);

        if (!out) {
            if (error)
                *error = L"Cannot write report ZIP";
            return false;
        }

        if (in.eof())
            break;

        if (!in) {
            if (error)
                *error = L"Cannot reread report file: " +
                         entry.source.wstring();
            return false;
        }
    }

    return true;
}

bool currentOffset(
    std::ofstream& out,
    std::uint32_t& value,
    std::wstring* error) {

    const auto position = out.tellp();
    if (position < 0) {
        if (error)
            *error = L"Cannot determine report ZIP offset";
        return false;
    }

    const auto streamOffset =
        static_cast<std::streamoff>(position);
    if (streamOffset < 0) {
        if (error)
            *error = L"Invalid report ZIP offset";
        return false;
    }

    const auto offset =
        static_cast<std::uint64_t>(streamOffset);
    if (offset >
        static_cast<std::uint64_t>(
            (std::numeric_limits<std::uint32_t>::max)())) {
        if (error)
            *error = L"Report ZIP exceeds classic ZIP limit";
        return false;
    }

    value = static_cast<std::uint32_t>(offset);
    return true;
}

bool writeNativeZip(
    const std::filesystem::path& outputZip,
    std::vector<ArchiveEntry>& entries,
    std::wstring* error) {

    if (entries.size() >
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint16_t>::max)())) {
        if (error)
            *error = L"Too many files for classic ZIP report";
        return false;
    }

    const auto temporary =
        std::filesystem::path(outputZip.wstring() + L".tmp");

    std::error_code ec;
    std::filesystem::remove(temporary, ec);

    std::ofstream out(
        temporary,
        std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error)
            *error = L"Cannot create report ZIP";
        return false;
    }

    for (auto& entry : entries) {
        if (entry.name.size() >
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint16_t>::max)())) {
            if (error)
                *error = L"Report ZIP path is too long";
            out.close();
            std::filesystem::remove(temporary, ec);
            return false;
        }

        if (!currentOffset(
                out,
                entry.localHeaderOffset,
                error)) {
            out.close();
            std::filesystem::remove(temporary, ec);
            return false;
        }

        const auto nameLength =
            static_cast<std::uint16_t>(entry.name.size());

        writeU32(out, kLocalFileHeaderSignature);
        writeU16(out, kZipVersion20);
        writeU16(out, kUtf8Flag);
        writeU16(out, kStoreMethod);
        writeU16(out, kDosTime);
        writeU16(out, kDosDate);
        writeU32(out, entry.crc32);
        writeU32(out, entry.size);
        writeU32(out, entry.size);
        writeU16(out, nameLength);
        writeU16(out, 0u);
        out.write(
            entry.name.data(),
            static_cast<std::streamsize>(entry.name.size()));

        if (!out ||
            !writeFileData(out, entry, error)) {
            out.close();
            std::filesystem::remove(temporary, ec);
            return false;
        }
    }

    std::uint32_t centralOffset = 0;
    if (!currentOffset(out, centralOffset, error)) {
        out.close();
        std::filesystem::remove(temporary, ec);
        return false;
    }

    for (const auto& entry : entries) {
        const auto nameLength =
            static_cast<std::uint16_t>(entry.name.size());

        writeU32(out, kCentralDirectorySignature);
        writeU16(out, kZipVersion20);
        writeU16(out, kZipVersion20);
        writeU16(out, kUtf8Flag);
        writeU16(out, kStoreMethod);
        writeU16(out, kDosTime);
        writeU16(out, kDosDate);
        writeU32(out, entry.crc32);
        writeU32(out, entry.size);
        writeU32(out, entry.size);
        writeU16(out, nameLength);
        writeU16(out, 0u);
        writeU16(out, 0u);
        writeU16(out, 0u);
        writeU16(out, 0u);
        writeU32(out, 0u);
        writeU32(out, entry.localHeaderOffset);
        out.write(
            entry.name.data(),
            static_cast<std::streamsize>(entry.name.size()));

        if (!out) {
            if (error)
                *error = L"Cannot write ZIP central directory";
            out.close();
            std::filesystem::remove(temporary, ec);
            return false;
        }
    }

    std::uint32_t endOffset = 0;
    if (!currentOffset(out, endOffset, error)) {
        out.close();
        std::filesystem::remove(temporary, ec);
        return false;
    }

    const std::uint32_t centralSize =
        endOffset - centralOffset;
    const auto entryCount =
        static_cast<std::uint16_t>(entries.size());

    writeU32(out, kEndOfCentralDirectorySignature);
    writeU16(out, 0u);
    writeU16(out, 0u);
    writeU16(out, entryCount);
    writeU16(out, entryCount);
    writeU32(out, centralSize);
    writeU32(out, centralOffset);
    writeU16(out, 0u);

    out.flush();
    if (!out) {
        if (error)
            *error = L"Cannot finalize report ZIP";
        out.close();
        std::filesystem::remove(temporary, ec);
        return false;
    }
    out.close();

    if (!MoveFileExW(
            temporary.c_str(),
            outputZip.c_str(),
            MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH)) {
        const DWORD moveError = GetLastError();
        if (error)
            *error = L"Cannot publish report ZIP: " +
                     std::to_wstring(moveError);
        std::filesystem::remove(temporary, ec);
        return false;
    }

    return true;
}
}

bool packageSessionReport(
    const std::filesystem::path& sessionDirectory,
    const std::filesystem::path& outputZip,
    std::wstring* error) {

    static constexpr std::array<const wchar_t*, 20>
        kReportFiles = {
            L"build_info.json",
            L"bodycam_logs_manifest.json",
            L"environment_capabilities.json",
            L"windows_events.json",
            L"session.log",
            L"session.json",
            L"system.json",
            L"presentmon.csv",
            L"telemetry.csv",
            L"video_capture.log",
            L"summary.json",
            L"summary.txt",
            L"validation.json",
            L"validation.txt",
            L"performance_health.json",
            L"performance_health.txt",
            L"stutter_events.json",
            L"stutter_events.txt",
            L"session_diagnostics.json",
            L"session_diagnostics.txt"
        };

    std::error_code ec;
    if (!std::filesystem::is_directory(
            sessionDirectory,
            ec)) {
        if (error)
            *error = L"Session directory does not exist";
        return false;
    }

    std::vector<ArchiveEntry> entries;
    for (const auto* name : kReportFiles) {
        if (!appendRegularFile(
                sessionDirectory,
                sessionDirectory / name,
                entries)) {
            if (error)
                *error = L"Cannot build report file list";
            return false;
        }
    }

    if (!appendDirectory(
            sessionDirectory,
            sessionDirectory / L"crash_diagnostics",
            entries) ||
        !appendDirectory(
            sessionDirectory,
            sessionDirectory / L"bodycam_logs",
            entries)) {
        if (error)
            *error = L"Cannot enumerate report directories";
        return false;
    }

    if (entries.empty()) {
        if (error)
            *error = L"No report files are available to package";
        return false;
    }

    std::sort(
        entries.begin(),
        entries.end(),
        [](const ArchiveEntry& a, const ArchiveEntry& b) {
            return a.name < b.name;
        });

    for (auto& entry : entries) {
        if (!analyzeFile(entry, error))
            return false;
    }

    if (!writeNativeZip(outputZip, entries, error))
        return false;

    ec.clear();
    if (!std::filesystem::is_regular_file(
            outputZip,
            ec) ||
        std::filesystem::file_size(outputZip, ec) == 0) {
        if (error)
            *error = L"report_bundle.zip was not created";
        return false;
    }

    return true;
}
