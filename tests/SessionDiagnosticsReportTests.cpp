#include "SessionDiagnostics.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}

std::string readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}
}

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        "BodycamDiagnostics-session-diagnostics-report-test";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);

    SessionDiagnosticsResult result{};
    result.captureValid = true;
    result.performanceClean = true;
    result.metricsAvailable = true;
    result.stutterCorrelationAvailable = true;
    result.signalCounts.push_back({"LOW_LEVEL_SIGNAL", 2});
    result.diagnosticIndicatorCounts.push_back({"CPU_STALL_INDICATOR", 1});

    DiagnosticTopEvent event{};
    event.timestamp = "2026-10-02T00:00:00Z";
    event.frameTimeMs = 250.0;
    event.presentMode = "Hardware Independent Flip";
    event.signals.push_back("LOW_LEVEL_SIGNAL");
    event.diagnosticIndicators.push_back("CPU_STALL_INDICATOR");
    result.topEvents.push_back(event);

    const auto jsonPath = root / "session_diagnostics.json";
    const auto textPath = root / "session_diagnostics.txt";

    std::wstring error;
    check(writeSessionDiagnosticsReport(result, jsonPath, textPath, &error),
          "session diagnostics report must serialize");

    const auto json = readAll(jsonPath);
    const auto text = readAll(textPath);

    check(json.find("\"signal_counts\"") != std::string::npos,
          "JSON must contain low-level signal counts");
    check(json.find("\"diagnostic_indicator_counts\"") != std::string::npos,
          "JSON must contain diagnostic indicator counts");
    check(json.find("\"signals\":[\"LOW_LEVEL_SIGNAL\"]") != std::string::npos,
          "JSON top event must serialize low-level signals separately");
    check(json.find("\"diagnostic_indicators\":[\"CPU_STALL_INDICATOR\"]") != std::string::npos,
          "JSON top event must serialize diagnostic indicators separately");

    check(text.find("SIGNAL COUNTS") != std::string::npos,
          "text report must contain signal counts section");
    check(text.find("DIAGNOSTIC INDICATOR COUNTS") != std::string::npos,
          "text report must contain diagnostic indicator counts section");
    check(text.find("signals=LOW_LEVEL_SIGNAL") != std::string::npos,
          "text top event must serialize low-level signals");
    check(text.find("indicators=CPU_STALL_INDICATOR") != std::string::npos,
          "text top event must serialize diagnostic indicators");

    std::filesystem::remove_all(root, ec);
    std::cout << "Session diagnostics report tests passed\n";
}
