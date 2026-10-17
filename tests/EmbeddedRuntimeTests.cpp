#include "EmbeddedRuntime.h"
#include "EmbeddedPayload.h"
#include "ToolPreflight.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main() {
    const auto root = std::filesystem::temp_directory_path() /
        (L"Bodycam-embedded-test-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec; std::filesystem::remove_all(root, ec);
    std::wstring error;
    check(unpackEmbeddedPayload(root, &error), "unpack without administrator access");
    const auto before = std::filesystem::last_write_time(root / L"PresentMon.exe");
    check(unpackEmbeddedPayload(root, &error), "second unpack is idempotent");
    check(before == std::filesystem::last_write_time(root / L"PresentMon.exe"), "verified tools are not rewritten");
    check(runtimeHashManifestTrusted(root / L"runtime-tools.sha256"), "trust anchor matches embedded manifest");
    for (const auto& asset : kEmbeddedAssets)
        check(fileSha256Hex(root / asset.name) == asset.sha, "resource bytes match expected SHA-256");
    std::ofstream(root / L"PresentMon.exe", std::ios::binary | std::ios::trunc) << "tampered";
    check(!unpackEmbeddedPayload(root, &error), "tampered runtime is rejected without execution");
    check(!error.empty(), "rejection explains the error");
    check(std::filesystem::file_size(root / L"PresentMon.exe") == 8, "tampered file is not silently overwritten");
    std::filesystem::remove_all(root, ec);
    std::ofstream(root) << "blocker";
    check(!unpackEmbeddedPayload(root, &error), "file blocking runtime directory is rejected");
    std::filesystem::remove(root, ec);
    std::cout << "Embedded runtime integration tests passed\n";
}
