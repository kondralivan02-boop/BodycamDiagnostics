#include "EnvironmentDiagnostics.h"
#include "TestValidator.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    check(argc == 2, "expected FFmpeg duration fixture path");
    const std::filesystem::path ffmpegFixture = argv[1];
    check(std::filesystem::is_regular_file(ffmpegFixture),
          "FFmpeg duration fixture must exist");
    const auto root = std::filesystem::temp_directory_path() / (L"BodycamDiagnostics-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    std::ofstream(root / L"video_with_audio.mkv") << "fixture";
    MetricsSummary metrics; metrics.valid = true;
    // Duration <=1 avoids invoking FFmpeg; this case isolates the optional-source policy.
    const auto valid = validateTestSession(root, metrics, 0.5, false, false, 0.5);
    check(valid.valid, "missing optional telemetry must not invalidate capture");
    check(!valid.warnings.empty(), "missing telemetry must be reported");
    check(!validateTestSession(root, metrics, 0.5, true, false, 0.5).valid, "game exit still invalidates capture");
    metrics.valid = false;
    check(!validateTestSession(root, metrics, 0.5, false, false, 0.5).valid, "missing metrics still invalidates capture");
    metrics.valid = true;
    std::filesystem::remove(root / L"video_with_audio.mkv");
    check(validateTestSession(root, metrics, 0.5, false, false).valid, "video disabled must allow valid diagnostics");
    check(!validateTestSession(root, metrics, 0.5, false, false, 0.0, true).valid,
          "failed recording request must not be reported as disabled video");
    check(!validateTestSession(root, metrics, 0.5, false, false, 0.4).valid, "requested video missing must invalidate capture");

    std::ofstream(root / L"video_without_audio.mkv") << "video-only fixture";
    check(validateTestSession(root, metrics, 0.5, false, false, 0.5).valid,
          "video_without_audio.mkv must satisfy required final-video validation");
    std::filesystem::remove(root / L"video_without_audio.mkv");

    const auto runtimeApp = root / L"runtime-app";
    const auto runtimeSession = runtimeApp / L"Sessions" / L"session";
    std::filesystem::create_directories(runtimeSession);
    std::filesystem::copy_file(
        ffmpegFixture,
        runtimeApp / L"ffmpeg.exe",
        std::filesystem::copy_options::overwrite_existing);
    std::ofstream(runtimeSession / L"video_with_audio.mkv") << "duration probe fixture";

    MetricsSummary durationMetrics;
    durationMetrics.valid = true;
    durationMetrics.durationSeconds = 10.0;
    const auto fallbackValidation =
        validateTestSession(
            runtimeSession,
            durationMetrics,
            10.0,
            false,
            true,
            10.0);
    check(fallbackValidation.valid,
          "validator must accept root-level FFmpeg runtime fallback");
    check(fallbackValidation.videoDurationChecked,
          "root-level FFmpeg fallback must be used for duration probe");

    const auto untrustedProbe =
        validateTestSession(
            runtimeSession,
            durationMetrics,
            10.0,
            false,
            false,
            10.0);
    check(!untrustedProbe.valid,
          "untrusted FFmpeg must block duration-probe validation");
    check(!untrustedProbe.videoDurationChecked,
          "untrusted FFmpeg must never be executed for duration probe");
    std::ofstream(runtimeSession / L"video_part_1_video_without_audio.mkv") << "first";
    const auto missingSecondPart = validateTestSession(
        runtimeSession, durationMetrics, 10.0, false, true, 10.0, false, {5.0, 5.0});
    check(!missingSecondPart.valid, "each recording interval needs its own video file");
    std::ofstream(runtimeSession / L"video_part_2_video_without_audio.mkv") << "second";
    const auto separateParts = validateTestSession(
        runtimeSession, durationMetrics, 10.0, false, true, 10.0, false, {5.0, 5.0});
    check(separateParts.valid, "separate completed intervals must validate independently");
    const auto shortPart = validateTestSession(
        runtimeSession, durationMetrics, 10.0, false, true, 0.5, false, {0.5});
    check(shortPart.videoDurationChecked && shortPart.videoDurationSeconds > 0.0,
          "short recording must actually be probed before claiming checked duration");
    const auto untrustedShortPart = validateTestSession(
        runtimeSession, durationMetrics, 10.0, false, false, 0.5, false, {0.5});
    check(!untrustedShortPart.valid && !untrustedShortPart.videoDurationChecked,
          "short recording duration must not bypass FFmpeg trust");
    const auto singleExeRoot = root / L"single-exe-layout";
    const auto singleExeSession = singleExeRoot / L"Sessions" / L"session";
    const auto embeddedRuntime = singleExeRoot / L"Runtime" / L"payload";
    std::filesystem::create_directories(singleExeSession);
    std::filesystem::create_directories(embeddedRuntime);
    const auto embeddedFfmpeg = embeddedRuntime / L"ffmpeg.exe";
    std::filesystem::copy_file(
        ffmpegFixture,
        embeddedFfmpeg,
        std::filesystem::copy_options::overwrite_existing);
    std::ofstream(singleExeSession / L"video_part_1_video_without_audio.mkv") << "single-exe part";

    const auto missingSingleExeProbePath = validateTestSession(
        singleExeSession,
        durationMetrics,
        10.0,
        false,
        true,
        10.0,
        false,
        {10.0});
    check(!missingSingleExeProbePath.valid,
          "legacy Sessions-relative FFmpeg lookup must not accidentally validate the single-exe layout");

    const auto singleExeValidation = validateTestSession(
        singleExeSession,
        durationMetrics,
        10.0,
        false,
        true,
        10.0,
        false,
        {10.0},
        embeddedFfmpeg);
    check(singleExeValidation.valid,
          "validator must use the exact trusted FFmpeg path supplied by the single-exe runtime");
    check(singleExeValidation.videoDurationChecked,
          "single-exe video duration must be probed with the supplied runtime FFmpeg");

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

    EventLogCollection infrastructureFixture;
    infrastructureFixture.events.push_back({
        "System", "iScsiPrt", 20, 1, baseMs + 1000, "<Event/>"});
    infrastructureFixture.events.push_back({
        "System", "disk", 153, 2, baseMs + 2000, "<Event/>"});
    infrastructureFixture.events.push_back({
        "System", "rt640x64", 27, 3, baseMs + 1500,
        "<Event><Data>Realtek PCIe GbE Family Controller</Data><Data Name=\"ResetCount\">6</Data></Event>"});
    infrastructureFixture.events.push_back({
        "System", "Microsoft-Windows-DNS-Client", 1014, 4, baseMs + 2500, "<Event/>"});

    const auto infrastructure =
        analyzeInfrastructureEvents(infrastructureFixture);
    check(infrastructure.instabilityObserved,
          "multi-source iSCSI/network/disk evidence must be classified as external instability");
    check(infrastructure.iscsiEvents == 1,
          "iSCSI event count");
    check(infrastructure.networkResetEvents == 1,
          "network reset event count");
    check(infrastructure.diskRetryEvents == 1,
          "disk retry event count");
    check(infrastructure.dnsTimeoutEvents == 1,
          "DNS timeout event count");
    check(writeInfrastructureEvidenceReport(
              infrastructure,
              root / L"infrastructure_evidence.json",
              root / L"infrastructure_evidence.txt"),
          "write infrastructure evidence report");

    check(writeTestValidationReport(valid, root / L"validation.json", root / L"validation.txt"), "write warning report");
    // Leave fixtures for the CI JSON parser, then remove with that step.
    std::cout << "REPORT_FIXTURES=" << root.string() << '\n';
    std::cout << "Windows diagnostics tests passed\n";
}
