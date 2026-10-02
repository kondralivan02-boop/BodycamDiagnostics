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

int wmain(int argc, wchar_t** argv) {
    check(argc == 2, "expected trusted runtime manifest path");
    const std::filesystem::path trustedManifest = argv[1];
    check(std::filesystem::is_regular_file(trustedManifest),
          "trusted runtime manifest must exist");

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

    const auto manifest = root / "runtime-tools.sha256";
    {
        std::ofstream out(manifest);
        out << "# test manifest\n";
        out << "f16d05ec6b29248d2c61adb1e9263f78e4f7bace1b955014a2d17872cfe4064d  PresentMon.exe\n";
    }
    check(expectedRuntimeToolSha256(manifest, L"PresentMon.exe") ==
              "f16d05ec6b29248d2c61adb1e9263f78e4f7bace1b955014a2d17872cfe4064d",
          "runtime tool hash must come from manifest");
    check(expectedRuntimeToolSha256(manifest, L"ffmpeg.exe").empty(),
          "missing manifest entry must not produce a trusted hash");

    const auto fakeFfmpeg = root / "ffmpeg.exe";
    std::ofstream(fakeFfmpeg) << "not-the-verified-ffmpeg";
    const auto trustedManifestPreflight =
        runToolPreflight(preferred, fakeFfmpeg, trustedManifest);
    check(trustedManifestPreflight.hashManifestFound,
          "repository runtime manifest should be found");
    check(trustedManifestPreflight.hashManifestTrusted,
          "repository runtime manifest must match the build trust anchor");
    check(runtimeHashManifestTrusted(trustedManifest),
          "trusted runtime manifest helper must accept build-anchored manifest");
    check(!runtimeHashManifestTrusted(manifest),
          "runtime manifest helper must reject a modified manifest");
    check(!runtimeToolTrusted(fakeFfmpeg, trustedManifest),
          "runtime trust helper must reject a modified executable");

    const auto preflight = runToolPreflight(preferred, fakeFfmpeg, manifest);
    check(preflight.hashManifestFound,
          "tampered manifest fixture should still be found");
    check(!preflight.hashManifestTrusted,
          "tampered runtime manifest must not be trusted");
    check(preflight.ffmpegFound, "test ffmpeg fixture should be found");
    check(!preflight.ffmpegTrusted, "unknown ffmpeg hash must not be trusted");
    check(!preflight.recordingReady(), "untrusted ffmpeg must block recording readiness");

    std::ofstream(preferred) << "fixture";
    check(resolveRuntimeTool(root, L"PresentMon.exe") == preferred,
          "Tools path must take priority when both copies exist");

    std::filesystem::remove_all(root, ec);
    std::cout << "Tool preflight tests passed\n";
}
