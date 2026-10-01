#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

struct MetricsSummary {
    bool valid = false;
    std::string frameTimeColumn;
    std::size_t totalFrames = 0;
    double durationSeconds = 0.0;
    double averageFps = 0.0;
    double averageFrameTimeMs = 0.0;
    double onePercentLowFps = 0.0;
    double pointOnePercentLowFps = 0.0;
    double p50FrameTimeMs = 0.0;
    double p95FrameTimeMs = 0.0;
    double p99FrameTimeMs = 0.0;
    double p999FrameTimeMs = 0.0;
    std::size_t stuttersOver50Ms = 0;
    std::size_t stuttersOver100Ms = 0;
    std::size_t stuttersOver250Ms = 0;
    double maxFrameTimeMs = 0.0;
    std::wstring error;
};

MetricsSummary calculateMetrics(const std::filesystem::path& presentMonCsv);
bool writeMetricsReport(const MetricsSummary& summary,
                        const std::filesystem::path& jsonOutput,
                        const std::filesystem::path& textOutput,
                        std::wstring* error = nullptr);
