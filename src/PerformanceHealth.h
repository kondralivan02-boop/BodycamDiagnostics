#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "MetricsCalculator.h"

struct PerformanceHealthResult {
    bool clean = true;
    double maxFrameTimeMs = 0.0;
    std::size_t severeStalls = 0;
    std::vector<std::wstring> findings;
};

PerformanceHealthResult analyzePerformanceHealth(const MetricsSummary& summary);

bool writePerformanceHealthReport(
    const PerformanceHealthResult& result,
    const std::filesystem::path& jsonOutput,
    const std::filesystem::path& textOutput,
    std::wstring* error = nullptr);
