#pragma once
#include "DiagnosticEvidence.h"
#include <chrono>
#include <filesystem>

struct BodycamLogFile {
    std::string filename;
    std::uintmax_t originalBytes = 0;
    std::uintmax_t copiedBytes = 0;
    bool tailOnly = false;
};
struct BodycamLogCollection {
    SourceCapability source{"bodycam_unreal_logs", "not_probed", 0, true, "Optional raw evidence; no per-line timestamp or causal inference"};
    std::vector<BodycamLogFile> files;
    std::uintmax_t bytesCopied = 0;
};
BodycamLogCollection collectBodycamLogs(const std::filesystem::path& sourceDirectory,
                                      const std::filesystem::path& sessionDirectory,
                                      std::chrono::system_clock::time_point sessionStart);
bool writeBodycamLogsManifest(const BodycamLogCollection& result, const std::filesystem::path& output);
