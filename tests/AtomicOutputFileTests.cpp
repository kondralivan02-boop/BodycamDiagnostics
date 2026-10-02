#include "AtomicOutputFile.h"

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

std::string readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string{
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()};
}
}

int main() {
    const auto root =
        std::filesystem::temp_directory_path() /
        L"BodycamDiagnostics-atomic-output-test";
    const auto output = root / L"report.json";
    const auto temporary = root / L"report.json.tmp";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);

    {
        std::ofstream(output, std::ios::binary | std::ios::trunc)
            << "old-complete-report";
    }

    {
        AtomicOutputFile abandoned(output);
        check(abandoned.isOpen(), "atomic output must open");
        abandoned.stream() << "partial-new-report";
    }

    check(readAll(output) == "old-complete-report",
          "abandoned write must preserve previous complete report");
    check(!std::filesystem::exists(temporary),
          "abandoned temporary file must be cleaned up");

    {
        AtomicOutputFile published(output);
        check(published.isOpen(), "atomic output must reopen");
        published.stream() << "new-complete-report";
        std::wstring error;
        check(published.commit(&error),
              "atomic output commit must succeed");
    }

    check(readAll(output) == "new-complete-report",
          "commit must atomically publish new report");
    check(!std::filesystem::exists(temporary),
          "committed temporary file must not remain");

    std::filesystem::remove_all(root, ec);
    std::cout << "Atomic output tests passed\n";
}
