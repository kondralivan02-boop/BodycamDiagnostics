#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "MetricsCalculator.h"

struct TestValidationResult {
    bool valid = false;
    double expectedDurationSeconds = 0.0;
    double presentMonCoverageRatio = 0.0;
    bool videoDurationChecked = false;
    double requestedVideoSeconds = 0.0;
    double videoDurationSeconds = 0.0;
    double videoCoverageRatio = 0.0;
    std::vector<std::wstring> issues;
    std::vector<std::wstring> warnings;
};

TestValidationResult validateTestSession(
    const std::filesystem::path& sessionPath,
    const MetricsSummary& summary,
    double expectedDurationSeconds,
    bool gameExitedUnexpectedly,
    bool ffmpegTrustedForProbe,
    double requestedVideoSeconds = 0.0,
    bool videoCaptureFailed = false,
    const std::vector<double>& requestedIntervals = {});

bool writeTestValidationReport(
    const TestValidationResult& result,
    const std::filesystem::path& jsonOutput,
    const std::filesystem::path& textOutput,
    std::wstring* error = nullptr);
