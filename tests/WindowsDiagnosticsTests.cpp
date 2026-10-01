#include "EnvironmentDiagnostics.h"
#include "TestValidator.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    const auto root = std::filesystem::temp_directory_path() / (L"BodycamDiagnostics-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    std::ofstream(root / L"video_with_audio.mkv") << "fixture";
    MetricsSummary metrics; metrics.valid = true;
    // Duration <=1 avoids invoking FFmpeg; this case isolates the optional-source policy.
    const auto valid = validateTestSession(root, metrics, 0.5, false);
    check(valid.valid, "missing optional telemetry must not invalidate capture");
    check(!valid.warnings.empty(), "missing telemetry must be reported");
    check(!validateTestSession(root, metrics, 0.5, true).valid, "game exit still invalidates capture");
    metrics.valid = false;
    check(!validateTestSession(root, metrics, 0.5, false).valid, "missing metrics still invalidates capture");
    metrics.valid = true;
    std::filesystem::remove(root / L"video_with_audio.mkv");
    check(!validateTestSession(root, metrics, 0.5, false).valid, "missing video still invalidates capture");
    const auto caps = probeEnvironmentCapabilities(GetCurrentProcessId());
    check(caps.sources.size() == 3, "both channels probed independently");
    check(caps.sources.front().status == "available", "current process query");
    const auto now = std::chrono::system_clock::now();
    const auto future = now + std::chrono::hours(24);
    const auto empty = collectWindowsEvents(future, future + std::chrono::seconds(1));
    check(empty.channels.size() == 2, "two source results");
    check(empty.events.empty(), "no future events");
    for (const auto& c : empty.channels)
        check(c.status == "available_empty" || c.status == "access_denied" || c.status == "not_found", "empty channel query status");
    check(saveEnvironmentCapabilities(caps, root / L"environment_capabilities.json"), "write profile");
    check(writeWindowsEventReport(empty, {}, now, now, now, true, false, root / L"windows_events.json"), "write empty event report");
    EventLogCollection partial;
    partial.channels.push_back({"event_log_System", "partial", ERROR_ACCESS_DENIED, true, "partial fixture"});
    const auto baseMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    partial.events.push_back({"System", "test-provider", 41, 123, baseMs + 4000, "<Event>\"\n</Event>"});
    StutterCorrelationResult stalls;
    StutterEvent stall;
    stall.relativeTimeValid = true;
    stall.relativeTimeMs = 1000;
    stall.frameTimeMs = 5000;
    stalls.events.push_back(stall);
    check(writeWindowsEventReport(partial, stalls, now, now, now + std::chrono::seconds(10), true, false, root / L"windows_events_partial.json"), "partial report");
    check(writeWindowsEventReport(partial, stalls, now, now, now + std::chrono::seconds(10), false, false, root / L"windows_events_clock_change.json"), "clock change report");
    check(writeTestValidationReport(valid, root / L"validation.json", root / L"validation.txt"), "write warning report");
    // Leave fixtures for the CI JSON parser, then remove with that step.
    std::cout << "REPORT_FIXTURES=" << root.string() << '\n';
    std::cout << "Windows diagnostics tests passed\n";
}
