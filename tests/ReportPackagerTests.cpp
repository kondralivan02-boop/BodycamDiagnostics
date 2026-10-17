#include "ReportPackager.h"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}

std::vector<unsigned char> readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    const std::vector<char> raw{
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()};

    return std::vector<unsigned char>(
        raw.begin(),
        raw.end());
}

std::uint16_t readU16(
    const std::vector<unsigned char>& bytes,
    std::size_t offset) {

    check(offset + 2 <= bytes.size(), "ZIP readU16 out of bounds");
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(bytes[offset + 1]) << 8u));
}

std::uint32_t readU32(
    const std::vector<unsigned char>& bytes,
    std::size_t offset) {

    check(offset + 4 <= bytes.size(), "ZIP readU32 out of bounds");
    return
        static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
}

std::uint32_t crc32(const unsigned char* data, std::size_t size) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

std::string readString(
    const std::vector<unsigned char>& bytes,
    std::size_t offset,
    std::size_t length) {

    check(offset + length <= bytes.size(), "ZIP string out of bounds");
    return std::string(
        reinterpret_cast<const char*>(bytes.data() + offset),
        length);
}

struct CentralEntry {
    std::uint16_t flags = 0;
    std::uint16_t method = 0;
    std::uint32_t crc = 0;
    std::uint32_t compressedSize = 0;
    std::uint32_t uncompressedSize = 0;
    std::uint32_t localOffset = 0;
};

std::map<std::string, CentralEntry> parseCentralDirectory(
    const std::vector<unsigned char>& bytes) {

    check(bytes.size() >= 22, "ZIP is smaller than EOCD");
    const std::size_t eocd = bytes.size() - 22;

    check(readU32(bytes, eocd) == 0x06054b50u,
          "ZIP EOCD signature is missing");
    check(readU16(bytes, eocd + 4) == 0u,
          "ZIP must use a single disk");
    check(readU16(bytes, eocd + 6) == 0u,
          "ZIP central directory must start on disk 0");
    check(readU16(bytes, eocd + 20) == 0u,
          "ZIP comment must be empty");

    const auto entriesOnDisk = readU16(bytes, eocd + 8);
    const auto totalEntries = readU16(bytes, eocd + 10);
    check(entriesOnDisk == totalEntries,
          "ZIP entry count mismatch");

    const auto centralSize = readU32(bytes, eocd + 12);
    const auto centralOffset = readU32(bytes, eocd + 16);
    check(
        static_cast<std::uint64_t>(centralOffset) +
                static_cast<std::uint64_t>(centralSize) ==
            static_cast<std::uint64_t>(eocd),
        "ZIP central directory size/offset mismatch");

    std::map<std::string, CentralEntry> entries;
    std::size_t offset = centralOffset;

    for (std::uint16_t i = 0; i < totalEntries; ++i) {
        check(readU32(bytes, offset) == 0x02014b50u,
              "ZIP central directory signature is missing");

        const auto flags = readU16(bytes, offset + 8);
        const auto method = readU16(bytes, offset + 10);
        const auto crc = readU32(bytes, offset + 16);
        const auto compressed = readU32(bytes, offset + 20);
        const auto uncompressed = readU32(bytes, offset + 24);
        const auto nameLength = readU16(bytes, offset + 28);
        const auto extraLength = readU16(bytes, offset + 30);
        const auto commentLength = readU16(bytes, offset + 32);
        const auto localOffset = readU32(bytes, offset + 42);

        const std::string name =
            readString(bytes, offset + 46, nameLength);

        check((flags & 0x0800u) != 0u,
              "ZIP filename must be marked UTF-8");
        check(method == 0u,
              "diagnostic ZIP must use store method");

        entries.emplace(
            name,
            CentralEntry{
                flags,
                method,
                crc,
                compressed,
                uncompressed,
                localOffset});

        offset +=
            46u +
            static_cast<std::size_t>(nameLength) +
            static_cast<std::size_t>(extraLength) +
            static_cast<std::size_t>(commentLength);
    }

    check(offset == eocd,
          "ZIP central directory does not end at EOCD");
    return entries;
}

void verifyStoredEntry(
    const std::vector<unsigned char>& bytes,
    const std::map<std::string, CentralEntry>& entries,
    const std::string& name,
    const std::string& expected) {

    const auto found = entries.find(name);
    check(found != entries.end(), "expected ZIP entry is missing");
    const auto& entry = found->second;

    const std::size_t offset = entry.localOffset;
    check(readU32(bytes, offset) == 0x04034b50u,
          "ZIP local header signature is missing");

    const auto flags = readU16(bytes, offset + 6);
    const auto method = readU16(bytes, offset + 8);
    const auto crc = readU32(bytes, offset + 14);
    const auto compressed = readU32(bytes, offset + 18);
    const auto uncompressed = readU32(bytes, offset + 22);
    const auto nameLength = readU16(bytes, offset + 26);
    const auto extraLength = readU16(bytes, offset + 28);

    check(flags == entry.flags, "ZIP flags differ between headers");
    check(method == entry.method, "ZIP method differs between headers");
    check(crc == entry.crc, "ZIP CRC differs between headers");
    check(compressed == entry.compressedSize,
          "ZIP compressed size differs between headers");
    check(uncompressed == entry.uncompressedSize,
          "ZIP uncompressed size differs between headers");

    const std::string localName =
        readString(bytes, offset + 30, nameLength);
    check(localName == name, "ZIP local filename mismatch");

    const std::size_t dataOffset =
        offset +
        30u +
        static_cast<std::size_t>(nameLength) +
        static_cast<std::size_t>(extraLength);

    check(compressed == expected.size(),
          "ZIP stored payload size mismatch");
    const std::string payload =
        readString(bytes, dataOffset, compressed);
    check(payload == expected, "ZIP payload differs from source");

    const auto calculatedCrc =
        crc32(
            reinterpret_cast<const unsigned char*>(expected.data()),
            expected.size());
    check(crc == calculatedCrc, "ZIP CRC does not match payload");
}
}

int wmain(int argc, wchar_t** argv) {
    check(argc == 2, "expected fixture executable");

    const std::filesystem::path fixture = argv[1];
    check(std::filesystem::is_regular_file(fixture),
          "fixture executable must exist");

    const auto root =
        std::filesystem::temp_directory_path() /
        L"BodycamDiagnostics-report-packager-test";
    const auto session = root / L"session";
    const auto output = root / L"report_bundle.zip";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(session / L"bodycam_logs");
    std::filesystem::create_directories(session / L"video" / L"part_1");

    constexpr const char* kSummary = "summary evidence";
    constexpr const char* kFfmpeg = "ffmpeg evidence";
    constexpr const char* kGameLog = "game log evidence";
    constexpr const char* kPresentMon = "PresentMon exit 6 diagnostic";
    std::ofstream(session / L"presentmon.csv.stderr.log", std::ios::binary) << kPresentMon;

    std::ofstream(session / L"summary.txt", std::ios::binary)
        << kSummary;
    std::ofstream(session / L"video_capture.log", std::ios::binary)
        << kFfmpeg;
    std::ofstream(session / L"video" / L"part_1" / L"video_capture.log", std::ios::binary)
        << "failed part evidence";
    std::ofstream(
        session / L"bodycam_logs" / L"Bodycam.log",
        std::ios::binary)
        << kGameLog;

    // Shadow tar.exe with a process that exits immediately.
    // Native packaging must never execute it.
    std::filesystem::copy_file(
        fixture,
        root / L"tar.exe",
        std::filesystem::copy_options::overwrite_existing);

    wchar_t oldDirectory[32768]{};
    const DWORD oldLength =
        GetCurrentDirectoryW(
            static_cast<DWORD>(std::size(oldDirectory)),
            oldDirectory);
    check(
        oldLength > 0 &&
            oldLength < std::size(oldDirectory),
        "cannot capture current directory");
    check(
        SetCurrentDirectoryW(root.c_str()) != FALSE,
        "cannot set test current directory");

    std::wstring error;
    const bool ok =
        packageSessionReport(session, output, &error);

    SetCurrentDirectoryW(oldDirectory);

    check(ok,
          "report packaging must not depend on external tar.exe");
    check(std::filesystem::is_regular_file(output),
          "report_bundle.zip must exist");

    const auto bytes = readAll(output);
    const auto entries = parseCentralDirectory(bytes);
    verifyStoredEntry(bytes, entries, "presentmon.csv.stderr.log", kPresentMon);

    check(entries.size() == 5u,
          "unexpected number of ZIP entries");
    verifyStoredEntry(
        bytes, entries, "summary.txt", kSummary);
    verifyStoredEntry(
        bytes, entries, "video_capture.log", kFfmpeg);
    verifyStoredEntry(
        bytes, entries, "video/part_1/video_capture.log", "failed part evidence");
    verifyStoredEntry(
        bytes,
        entries,
        "bodycam_logs/Bodycam.log",
        kGameLog);

    std::filesystem::remove_all(root, ec);
    std::cout << "Report packager tests passed\n";
}
