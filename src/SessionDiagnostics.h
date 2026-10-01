#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "MetricsCalculator.h"
#include "PerformanceHealth.h"
#include "StutterCorrelator.h"
#include "TestValidator.h"

struct DiagnosticSignalCount {
    std::string signal;
    std::size_t count = 0;
};

struct DiagnosticTopEvent {
    std::string timestamp;
    bool videoBookmarkValid = false;
    double videoBookmarkMs = 0.0;
    double frameTimeMs = 0.0;
    std::string presentMode;
    bool telemetryMatched = false;
    double gpuUtilPct = 0.0;
    double gpuGraphicsMHz = 0.0;
    double gpuPowerW = 0.0;
    std::vector<std::string> signals;
    std::vector<std::string> diagnosticIndicators;
};

struct SessionDiagnosticsResult {
    bool captureValid = false;
    bool performanceClean = false;
    bool metricsAvailable = false;
    bool stutterCorrelationAvailable = false;

    std::size_t totalFrames = 0;
    double durationSeconds = 0.0;
    double averageFps = 0.0;
    double onePercentLowFps = 0.0;
    double pointOnePercentLowFps = 0.0;
    double maxFrameTimeMs = 0.0;
    std::size_t stuttersOver100Ms = 0;
    std::size_t stuttersOver250Ms = 0;

    std::size_t correlatedStutters = 0;
    std::size_t telemetryMatchedStutters = 0;

    std::vector<DiagnosticSignalCount> signalCounts;
    std::vector<DiagnosticSignalCount> diagnosticIndicatorCounts;
    std::vector<DiagnosticTopEvent> topEvents;
    std::vector<std::wstring> validationIssues;
    std::vector<std::wstring> diagnosticLimitations;
    std::vector<std::wstring> performanceFindings;
};

SessionDiagnosticsResult analyzeSessionDiagnostics(
    const MetricsSummary& metrics,
    const TestValidationResult& validation,
    const PerformanceHealthResult& performance,
    const StutterCorrelationResult& stutters,
    std::size_t topEventLimit = 5);

bool writeSessionDiagnosticsReport(
    const SessionDiagnosticsResult& result,
    const std::filesystem::path& jsonOutput,
    const std::filesystem::path& textOutput,
    std::wstring* error = nullptr);

