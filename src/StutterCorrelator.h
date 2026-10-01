#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct StutterEvent {
    std::string timestamp;
    double frameTimeMs = 0.0;
    double cpuBusyMs = 0.0;
    double cpuWaitMs = 0.0;
    double gpuTimeMs = 0.0;
    double gpuBusyMs = 0.0;
    double gpuWaitMs = 0.0;
    std::string presentMode;

    bool telemetryMatched = false;
    double telemetryDeltaMs = 0.0;
    double processCpuPct = 0.0;
    double systemCpuPct = 0.0;
    double workingSetMiB = 0.0;
    double privateUsageMiB = 0.0;
    double systemMemoryLoadPct = 0.0;
    double systemMemoryAvailableMiB = 0.0;
    double gpuUtilPct = 0.0;
    double gpuMemoryUtilPct = 0.0;
    double vramUsedMiB = 0.0;
    double vramTotalMiB = 0.0;
    double gpuTempC = 0.0;
    double gpuGraphicsMHz = 0.0;
    double gpuMemoryMHz = 0.0;
    double gpuPowerW = 0.0;
    double gpuPowerLimitW = 0.0;
    std::vector<std::string> signals;
};

struct StutterCorrelationResult {
    bool valid = false;
    std::vector<StutterEvent> events;
    std::wstring error;
};

StutterCorrelationResult correlateStutterEvents(
    const std::filesystem::path& presentMonCsv,
    const std::filesystem::path& telemetryCsv,
    double thresholdMs = 100.0);

bool writeStutterCorrelationReport(
    const StutterCorrelationResult& result,
    const std::filesystem::path& jsonOutput,
    const std::filesystem::path& textOutput,
    std::wstring* error = nullptr);
