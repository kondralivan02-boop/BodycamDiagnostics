#include "MetricsCalculator.h"
#include "StutterCorrelator.h"
#include "SessionDiagnostics.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    const auto file = std::filesystem::temp_directory_path() / L"bodycam_capture_gap_fixture.csv";
    {
        std::ofstream csv(file, std::ios::binary);
        csv << "CPUStartDateTime,FrameTime,PresentMode\n"
            << "2026-10-02 10:00:00.000,16.7,Independent Flip\n"
            << "2026-10-02 10:00:00.017,16.7,Independent Flip\n"
            << "2026-10-02 10:00:42.600,42583,Composed Flip\n"
            << "2026-10-02 10:00:42.617,16.7,Composed Flip\n"
            << "2026-10-02 10:00:42.634,16.7,Composed Flip\n";
    }
    const auto metrics = calculateMetrics(file);
    check(metrics.valid && metrics.captureGaps == 1, "gap must be identified");
    check(metrics.maxFrameTimeMs == 16.7 && metrics.stuttersOver100Ms == 0, "gap must not count as a frame stall");
    const auto correlation = correlateStutterEvents(file, {}, 100.0, false, 0.0);
    check(correlation.valid && correlation.events.empty(), "gap must not correlate as stutter");
    TestValidationResult validation;
    const auto health = analyzePerformanceHealth(metrics);
    check(!health.clean, "capture gap contaminates performance comparison");
    const auto diagnostics = analyzeSessionDiagnostics(metrics, validation, health, correlation);
    check(diagnostics.maxFrameTimeMs == metrics.maxFrameTimeMs, "reports must agree on max frame time");
    check(diagnostics.captureGaps == 1, "session diagnostics must expose CAPTURE_GAP count");
    std::filesystem::remove(file);
}
