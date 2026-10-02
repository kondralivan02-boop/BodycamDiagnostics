#include "BuildInfo.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
}

int main() {
    const auto root =
        std::filesystem::temp_directory_path() /
        L"BodycamDiagnostics-build-info-test";
    const auto path = root / L"build_info.json";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);

    std::wstring error;
    check(writeBuildInfoReport(path, &error),
          "build provenance report must be written");
    check(std::filesystem::is_regular_file(path),
          "build_info.json must exist");

    std::ifstream in(path, std::ios::binary);
    const std::string text{
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()};

    check(text.find("\"schema_version\": 1") != std::string::npos,
          "build info schema version is missing");
    check(text.find("\"commit_sha\"") != std::string::npos,
          "build commit SHA is missing");
    check(text.find("\"branch\"") != std::string::npos,
          "build branch is missing");
    check(text.find("\"ci_run\"") != std::string::npos,
          "CI run identifier is missing");

    std::filesystem::remove_all(root, ec);
    std::cout << "Build info tests passed\n";
}
