#include "TestValidator.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {
std::string narrowAscii(const std::wstring& text) {
    std::string out;
    out.reserve(text.size());
    for (wchar_t c : text)
        out.push_back(c >= 0 && c <= 0x7F ? static_cast<char>(c) : '?');
    return out;
}

std::string jsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
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

TestValidationResult validateTestSession(
    const std::filesystem::path& sessionPath,
    const MetricsSummary& summary,
    double expectedDurationSeconds,
    bool gameExitedUnexpectedly) {

    TestValidationResult result;
    result.expectedDurationSeconds = expectedDurationSeconds;

    if (gameExitedUnexpectedly)
        result.issues.push_back(L"Bodycam exited or crashed during the test");

    if (!summary.valid) {
        result.issues.push_back(L"PresentMon metrics are unavailable");
    } else if (expectedDurationSeconds > 1.0) {
        result.presentMonCoverageRatio =
            summary.durationSeconds / expectedDurationSeconds;

        if (result.presentMonCoverageRatio < 0.90) {
            std::wstringstream ss;
            ss << L"PresentMon captured only "
               << std::fixed << std::setprecision(1)
               << result.presentMonCoverageRatio * 100.0
               << L"% of the test duration";
            result.issues.push_back(ss.str());
        }
    }

    std::error_code ec;
    const auto video = sessionPath / L"video_with_audio.mkv";
    if (!std::filesystem::is_regular_file(video, ec) ||
        std::filesystem::file_size(video, ec) == 0) {
        result.issues.push_back(L"Final video_with_audio.mkv is missing or empty");
    }

    ec.clear();
    const auto telemetry = sessionPath / L"telemetry.csv";
    if (!std::filesystem::is_regular_file(telemetry, ec) ||
        std::filesystem::file_size(telemetry, ec) == 0) {
        result.issues.push_back(L"telemetry.csv is missing or empty");
    }

    result.valid = result.issues.empty();
    return result;
}

bool writeTestValidationReport(
    const TestValidationResult& r,
    const std::filesystem::path& jsonOutput,
    const std::filesystem::path& textOutput,
    std::wstring* error) {

    std::ofstream json(jsonOutput, std::ios::binary | std::ios::trunc);
    if (!json) {
        if (error) *error = L"Cannot create validation.json";
        return false;
    }

    json << std::fixed << std::setprecision(3)
         << "{\n"
         << "  \"valid\": " << (r.valid ? "true" : "false") << ",\n"
         << "  \"expected_duration_seconds\": "
         << r.expectedDurationSeconds << ",\n"
         << "  \"presentmon_coverage_ratio\": "
         << r.presentMonCoverageRatio << ",\n"
         << "  \"issues\": [";

    for (std::size_t i = 0; i < r.issues.size(); ++i) {
        if (i) json << ", ";
        json << "\"" << jsonEscape(narrowAscii(r.issues[i])) << "\"";
    }

    json << "]\n}\n";

    std::ofstream text(textOutput, std::ios::binary | std::ios::trunc);
    if (!text) {
        if (error) *error = L"Cannot create validation.txt";
        return false;
    }

    text << (r.valid ? "VALID TEST\n" : "INVALID TEST\n")
         << std::fixed << std::setprecision(2)
         << "Expected duration: " << r.expectedDurationSeconds << " s\n"
         << "PresentMon coverage: "
         << r.presentMonCoverageRatio * 100.0 << "%\n";

    for (const auto& issue : r.issues)
        text << "- " << narrowAscii(issue) << "\n";

    return true;
}
