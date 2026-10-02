#include "ReportPackager.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}

std::vector<char> readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<char>(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}

bool contains(const std::vector<char>& bytes, const std::string& needle) {
    return std::search(
               bytes.begin(), bytes.end(),
               needle.begin(), needle.end()) != bytes.end();
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

    std::ofstream(session / L"summary.txt", std::ios::binary)
        << "summary evidence";
    std::ofstream(session / L"video_capture.log", std::ios::binary)
        << "ffmpeg evidence";
    std::ofstream(session / L"bodycam_logs" / L"Bodycam.log", std::ios::binary)
        << "game log evidence";

    // Shadow tar.exe with a process that exits immediately. Native packaging
    // must not execute it.
    std::filesystem::copy_file(
        fixture,
        root / L"tar.exe",
        std::filesystem::copy_options::overwrite_existing);

    wchar_t oldDirectory[32768]{};
    const DWORD oldLength =
        GetCurrentDirectoryW(static_cast<DWORD>(std::size(oldDirectory)),
                             oldDirectory);
    check(oldLength > 0 && oldLength < std::size(oldDirectory),
          "cannot capture current directory");
    check(SetCurrentDirectoryW(root.c_str()) != FALSE,
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
    check(bytes.size() > 22, "ZIP output is unexpectedly small");
    check(bytes[0] == 'P' && bytes[1] == 'K' &&
              static_cast<unsigned char>(bytes[2]) == 3 &&
              static_cast<unsigned char>(bytes[3]) == 4,
          "ZIP local header signature is missing");
    check(contains(bytes, "summary.txt"),
          "ZIP must contain summary.txt");
    check(contains(bytes, "video_capture.log"),
          "ZIP must contain video_capture.log");
    check(contains(bytes, "bodycam_logs/Bodycam.log"),
          "ZIP must contain nested Bodycam log");
    check(contains(bytes, std::string("PK\x05\x06", 4)),
          "ZIP end-of-central-directory signature is missing");

    std::filesystem::remove_all(root, ec);
    std::cout << "Report packager tests passed\n";
}
