#include "SessionDiagnostics.h"

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace {
std::string wideToUtf8(const std::wstring& value) {
    if (value.empty())
        return {};

    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (required <= 0)
        return {};

    std::string out(static_cast<std::size_t>(required), '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        out.data(),
        required,
        nullptr,
        nullptr);

    return out;
}

std::string jsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);

    for (const char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c); break;
        }
    }

    return out;
}

void writeWideStringArray(
    std::ofstream& out,
    const std::vector<std::wstring>& values,
    const char* indent) {

    out << "[";
    if (!values.empty())
        out << "\n";

    for (std::size_t i = 0; i < values.size(); ++i) {
        out << indent
            << "\""
            << jsonEscape(wideToUtf8(values[i]))
            << "\"";

        if (i + 1 < values.size())
            out << ",";

        out << "\n";
    }

    if (!values.empty())
        out << "    ";

    out << "]";
}
}

SessionDiagnosticsResult analyzeSessionDiagnostics(
    const MetricsSummary& metrics,
    const TestValidationResult& validation,
    const PerformanceHealthResult& performance,
    const StutterCorrelationResult& stutters,
    std::size_t topEventLimit) {

    SessionDiagnosticsResult result{};

    result.captureValid = validation.valid;
    result.performanceClean = performance.clean;
    result.metricsAvailable = metrics.valid;
    result.stutterCorrelationAvailable = stutters.valid;

    result.totalFrames = metrics.totalFrames;
    result.durationSeconds = metrics.durationSeconds;
    result.averageFps = metrics.averageFps;
    result.onePercentLowFps = metrics.onePercentLowFps;
    result.pointOnePercentLowFps = metrics.pointOnePercentLowFps;
    result.maxFrameTimeMs = metrics.maxFrameTimeMs;
    result.stuttersOver100Ms = metrics.stuttersOver100Ms;
    result.stuttersOver250Ms = metrics.stuttersOver250Ms;

    result.validationIssues = validation.issues;
    result.diagnosticLimitations = validation.warnings;
    result.performanceFindings = performance.findings;

    std::map<std::string, std::size_t> signalCounts;
    std::vector<const StutterEvent*> sortedEvents;
    sortedEvents.reserve(stutters.events.size());

    for (const auto& event : stutters.events) {
        ++result.correlatedStutters;

        if (event.telemetryMatched)
            ++result.telemetryMatchedStutters;

        if (event.frameTimeMs > result.maxFrameTimeMs)
            result.maxFrameTimeMs = event.frameTimeMs;

        for (const auto& signal : event.signals)
            ++signalCounts[signal];

        sortedEvents.push_back(&event);
    }

    result.signalCounts.reserve(signalCounts.size());
    for (const auto& [signal, count] : signalCounts)
        result.signalCounts.push_back({signal, count});

    std::sort(
        result.signalCounts.begin(),
        result.signalCounts.end(),
        [](const DiagnosticSignalCount& a, const DiagnosticSignalCount& b) {
            if (a.count != b.count)
                return a.count > b.count;
            return a.signal < b.signal;
        });

    std::sort(
        sortedEvents.begin(),
        sortedEvents.end(),
        [](const StutterEvent* a, const StutterEvent* b) {
            return a->frameTimeMs > b->frameTimeMs;
        });

    const std::size_t eventCount =
        std::min(topEventLimit, sortedEvents.size());

    result.topEvents.reserve(eventCount);

    for (std::size_t i = 0; i < eventCount; ++i) {
        const auto& event = *sortedEvents[i];

        DiagnosticTopEvent top{};
        top.timestamp = event.timestamp;
        top.frameTimeMs = event.frameTimeMs;
        top.presentMode = event.presentMode;
        top.telemetryMatched = event.telemetryMatched;
        top.gpuUtilPct = event.gpuUtilPct;
        top.gpuGraphicsMHz = event.gpuGraphicsMHz;
        top.gpuPowerW = event.gpuPowerW;
        top.signals = event.signals;

        result.topEvents.push_back(std::move(top));
    }

    return result;
}

bool writeSessionDiagnosticsReport(
    const SessionDiagnosticsResult& result,
    const std::filesystem::path& jsonOutput,
    const std::filesystem::path& textOutput,
    std::wstring* error) {

    std::ofstream json(jsonOutput, std::ios::binary);
    if (!json) {
        if (error)
            *error = L"Cannot create session diagnostics JSON";
        return false;
    }

    json << std::fixed << std::setprecision(3);
    json << "{\n";
    json << "  \"schema_version\": 1,\n";
    json << "  \"capture_status\": \""
         << (result.captureValid ? "VALID" : "INVALID")
         << "\",\n";
    json << "  \"performance_status\": \""
         << (result.performanceClean ? "CLEAN" : "CONTAMINATED")
         << "\",\n";
    json << "  \"metrics_available\": "
         << (result.metricsAvailable ? "true" : "false")
         << ",\n";
    json << "  \"stutter_correlation_available\": "
         << (result.stutterCorrelationAvailable ? "true" : "false")
         << ",\n";

    json << "  \"metrics\": {"
         << "\"total_frames\":" << result.totalFrames
         << ",\"duration_s\":" << result.durationSeconds
         << ",\"average_fps\":" << result.averageFps
         << ",\"one_percent_low_fps\":" << result.onePercentLowFps
         << ",\"point_one_percent_low_fps\":" << result.pointOnePercentLowFps
         << ",\"max_frame_time_ms\":" << result.maxFrameTimeMs
         << ",\"stutters_over_100_ms\":" << result.stuttersOver100Ms
         << ",\"stutters_over_250_ms\":" << result.stuttersOver250Ms
         << "},\n";

    json << "  \"stutter_summary\": {"
         << "\"correlated_events\":" << result.correlatedStutters
         << ",\"telemetry_matched_events\":" << result.telemetryMatchedStutters
         << "},\n";

    json << "  \"signal_counts\": [";
    if (!result.signalCounts.empty())
        json << "\n";

    for (std::size_t i = 0; i < result.signalCounts.size(); ++i) {
        const auto& signal = result.signalCounts[i];

        json << "    {\"signal\":\""
             << jsonEscape(signal.signal)
             << "\",\"count\":"
             << signal.count
             << "}";

        if (i + 1 < result.signalCounts.size())
            json << ",";

        json << "\n";
    }

    json << "  ],\n";

    json << "  \"top_stutters\": [";
    if (!result.topEvents.empty())
        json << "\n";

    for (std::size_t i = 0; i < result.topEvents.size(); ++i) {
        const auto& event = result.topEvents[i];

        json << "    {"
             << "\"timestamp\":\"" << jsonEscape(event.timestamp) << "\""
             << ",\"frame_time_ms\":" << event.frameTimeMs
             << ",\"present_mode\":\"" << jsonEscape(event.presentMode) << "\""
             << ",\"telemetry_matched\":"
             << (event.telemetryMatched ? "true" : "false")
             << ",\"gpu_util_pct\":" << event.gpuUtilPct
             << ",\"gpu_graphics_mhz\":" << event.gpuGraphicsMHz
             << ",\"gpu_power_w\":" << event.gpuPowerW
             << ",\"signals\":[";

        for (std::size_t s = 0; s < event.signals.size(); ++s) {
            json << "\"" << jsonEscape(event.signals[s]) << "\"";
            if (s + 1 < event.signals.size())
                json << ",";
        }

        json << "]}";

        if (i + 1 < result.topEvents.size())
            json << ",";

        json << "\n";
    }

    json << "  ],\n";

    json << "  \"diagnostic_limitations\": ";
    writeWideStringArray(json, result.diagnosticLimitations, "    ");
    json << ",\n";

    json << "  \"validation_issues\": ";
    writeWideStringArray(json, result.validationIssues, "    ");
    json << ",\n";

    json << "  \"performance_findings\": ";
    writeWideStringArray(json, result.performanceFindings, "    ");
    json << ",\n";

    json << "  \"interpretation\": "
         << "\"Signals are diagnostic indicators and do not by themselves prove root cause.\"\n";
    json << "}\n";

    if (!json) {
        if (error)
            *error = L"Failed while writing session diagnostics JSON";
        return false;
    }

    std::ofstream text(textOutput, std::ios::binary);
    if (!text) {
        if (error)
            *error = L"Cannot create session diagnostics text report";
        return false;
    }

    text << std::fixed << std::setprecision(2);
    text << "BODYCAM DIAGNOSTICS SESSION SUMMARY\n";
    text << "Capture: "
         << (result.captureValid ? "VALID" : "INVALID")
         << "\n";
    text << "Performance: "
         << (result.performanceClean ? "CLEAN" : "CONTAMINATED")
         << "\n";
    text << "Metrics available: "
         << (result.metricsAvailable ? "yes" : "no")
         << "\n";
    text << "Stutter correlation available: "
         << (result.stutterCorrelationAvailable ? "yes" : "no")
         << "\n\n";

    for (const auto& limitation : result.diagnosticLimitations)
        text << "Diagnostic limitation: " << wideToUtf8(limitation) << "\n";

    text << "Frames: " << result.totalFrames << "\n";
    text << "Duration: " << result.durationSeconds << " s\n";
    text << "Average FPS: " << result.averageFps << "\n";
    text << "1% Low: " << result.onePercentLowFps << " FPS\n";
    text << "0.1% Low: " << result.pointOnePercentLowFps << " FPS\n";
    text << "Max frame time: " << result.maxFrameTimeMs << " ms\n";
    text << "Stutters >=100 ms: " << result.stuttersOver100Ms << "\n";
    text << "Stutters >=250 ms: " << result.stuttersOver250Ms << "\n";
    text << "Correlated stutters: " << result.correlatedStutters << "\n";
    text << "Telemetry matched: " << result.telemetryMatchedStutters << "\n\n";

    text << "SIGNAL COUNTS\n";
    if (result.signalCounts.empty()) {
        text << "none\n";
    } else {
        for (const auto& signal : result.signalCounts)
            text << signal.count << " x " << signal.signal << "\n";
    }

    text << "\nTOP STUTTERS\n";
    if (result.topEvents.empty()) {
        text << "none\n";
    } else {
        for (const auto& event : result.topEvents) {
            text << event.frameTimeMs << " ms"
                 << " | " << event.timestamp
                 << " | " << event.presentMode
                 << " | telemetry="
                 << (event.telemetryMatched ? "yes" : "no");

            if (!event.signals.empty()) {
                text << " | signals=";
                for (std::size_t i = 0; i < event.signals.size(); ++i) {
                    if (i > 0)
                        text << ",";
                    text << event.signals[i];
                }
            }

            text << "\n";
        }
    }

    text << "\nVALIDATION ISSUES\n";
    if (result.validationIssues.empty()) {
        text << "none\n";
    } else {
        for (const auto& issue : result.validationIssues)
            text << "- " << wideToUtf8(issue) << "\n";
    }

    text << "\nPERFORMANCE FINDINGS\n";
    if (result.performanceFindings.empty()) {
        text << "none\n";
    } else {
        for (const auto& finding : result.performanceFindings)
            text << "- " << wideToUtf8(finding) << "\n";
    }

    text << "\nInterpretation: signals are diagnostic indicators; "
            "they do not by themselves prove root cause.\n";

    if (!text) {
        if (error)
            *error = L"Failed while writing session diagnostics text report";
        return false;
    }

    return true;
}

