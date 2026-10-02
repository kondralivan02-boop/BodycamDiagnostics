#include "PerformanceHealth.h"
#include "AtomicOutputFile.h"
#include <Windows.h>

#include <fstream>
#include <iomanip>
#include <sstream>

namespace {
std::string narrowAscii(const std::wstring& text) {
    if (text.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string out(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), bytes, nullptr, nullptr);
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
}

PerformanceHealthResult analyzePerformanceHealth(const MetricsSummary& summary) {
    PerformanceHealthResult result;

    if (!summary.valid) {
        result.clean = false;
        result.findings.push_back(L"Performance metrics are unavailable");
        return result;
    }

    result.maxFrameTimeMs = summary.maxFrameTimeMs;
    result.severeStalls = summary.stuttersOver250Ms;

    if (summary.captureGaps > 0) {
        result.clean = false;
        result.findings.push_back(L"CAPTURE_GAP: потеря кадров PresentMon (" + std::to_wstring(summary.captureGaps) + L"); сравнение FPS ограничено");
    }

    if (summary.maxFrameTimeMs >= 250.0) {
        result.clean = false;
        std::wstringstream ss;
        ss << L"Severe frametime stall detected: "
           << std::fixed << std::setprecision(1)
           << summary.maxFrameTimeMs << L" ms";
        result.findings.push_back(ss.str());
    }

    if (summary.stuttersOver250Ms > 1) {
        result.clean = false;
        result.findings.push_back(
            L"Multiple severe stalls >=250 ms: " +
            std::to_wstring(summary.stuttersOver250Ms));
    }

    if (summary.stuttersOver100Ms >= 5) {
        result.clean = false;
        result.findings.push_back(
            L"Frequent stalls >=100 ms: " +
            std::to_wstring(summary.stuttersOver100Ms));
    }

    return result;
}

bool writePerformanceHealthReport(
    const PerformanceHealthResult& r,
    const std::filesystem::path& jsonOutput,
    const std::filesystem::path& textOutput,
    std::wstring* error) {

    AtomicOutputFile jsonFile(jsonOutput);
    auto& json = jsonFile.stream();
    if (!jsonFile.isOpen()) {
        if (error) *error = L"Cannot create performance_health.json";
        return false;
    }

    json << std::fixed << std::setprecision(3)
         << "{\n"
         << "  \"clean\": " << (r.clean ? "true" : "false") << ",\n"
         << "  \"max_frame_time_ms\": " << r.maxFrameTimeMs << ",\n"
         << "  \"severe_stalls_over_250ms\": " << r.severeStalls << ",\n"
         << "  \"findings\": [";

    for (std::size_t i = 0; i < r.findings.size(); ++i) {
        if (i) json << ", ";
        json << "\"" << jsonEscape(narrowAscii(r.findings[i])) << "\"";
    }
    json << "]\n}\n";

    AtomicOutputFile textFile(textOutput);
    auto& text = textFile.stream();
    if (!textFile.isOpen()) {
        if (error) *error = L"Cannot create performance_health.txt";
        return false;
    }

    text << (r.clean ? "PERFORMANCE CLEAN\n" : "PERFORMANCE CONTAMINATED\n")
         << std::fixed << std::setprecision(2)
         << "Maximum frametime: " << r.maxFrameTimeMs << " ms\n"
         << "Severe stalls >=250 ms: " << r.severeStalls << "\n";

    for (const auto& finding : r.findings)
        text << "- " << narrowAscii(finding) << "\n";

    if (!json) {
        if (error)
            *error = L"Failed while writing performance_health.json";
        return false;
    }

    if (!text) {
        if (error)
            *error = L"Failed while writing performance_health.txt";
        return false;
    }

    if (!jsonFile.commit(error))
        return false;
    if (!textFile.commit(error))
        return false;

    return true;
}
