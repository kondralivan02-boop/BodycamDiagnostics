#include "ToolPreflight.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
}

int main() {
    const auto root = std::filesystem::temp_directory_path() / "BodycamDiagnostics-tool-resolution-test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "Tools");

    const auto preferred = root / "Tools" / "PresentMon.exe";
    const auto fallback = root / "PresentMon.exe";

    check(resolveRuntimeTool(root, L"PresentMon.exe") == preferred,
          "missing tool should resolve to preferred Tools path");

    std::ofstream(fallback) << "fixture";
    check(resolveRuntimeTool(root, L"PresentMon.exe") == fallback,
          "root-level tool should be accepted when Tools copy is absent");

    check(fileSha256Hex(fallback) ==
              "f16d05ec6b29248d2c61adb1e9263f78e4f7bace1b955014a2d17872cfe4064d",
          "SHA-256 must be computed exactly for runtime tools");

    std::ofstream(preferred) << "fixture";
    check(resolveRuntimeTool(root, L"PresentMon.exe") == preferred,
          "Tools path must take priority when both copies exist");

    std::filesystem::remove_all(root, ec);
    std::cout << "Tool preflight tests passed\n";
}
