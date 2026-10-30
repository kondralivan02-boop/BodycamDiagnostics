#include "AutoTuneFeedback.h"
#include "AtomicOutputFile.h"
#include <fstream>
#include <iomanip>

namespace {
bool gpuBusyEvidence(const StutterEvent& e) {
    return e.gpuBusyMs >= 100.0 && e.gpuBusyMs >= e.frameTimeMs * 0.25;
}
bool cpuBusyEvidence(const StutterEvent& e) {
    return e.cpuBusyMs >= 100.0 && e.cpuBusyMs >= e.frameTimeMs * 0.50;
}
}

AutoTuneFeedback analyzeAutoTuneFeedback(const StutterCorrelationResult& c) {
    AutoTuneFeedback r;
    r.sourceValid = c.valid;
    if (!c.valid) {
        r.explanation = "F9 stutter correlation is unavailable; no profile change is recommended.";
        return r;
    }
    r.stutterEvents = c.events.size();
    for (const auto& e : c.events) {
        if (e.telemetryMatched) ++r.telemetryMatchedEvents;
        if (e.processGpu3dValid) ++r.gpuMetricCoverageEvents;
        if (e.processMaxThreadCpuValid) ++r.cpuMetricCoverageEvents;
        const bool gpu = e.processGpu3dValid && e.processGpu3dMaxPct >= 90.0 && gpuBusyEvidence(e);
        const bool cpu = e.processMaxThreadCpuValid && e.processMaxThreadCpuPct >= 90.0 && cpuBusyEvidence(e);
        if (gpu && cpu) ++r.mixedPressureEvents;
        else if (gpu) ++r.gpuPressureEvents;
        else if (cpu) ++r.cpuPressureEvents;
    }
    if (r.stutterEvents == 0) {
        r.explanation = "No stutter events >=100 ms were captured. No graphics-profile change is indicated.";
        return r;
    }
    const auto coverage = [](std::size_t n, std::size_t d, std::size_t p) {
        return d != 0 && n * 100 >= d * p;
    };
    const auto classified = r.gpuPressureEvents + r.cpuPressureEvents + r.mixedPressureEvents;
    const bool covered = r.telemetryMatchedEvents >= 2 &&
        coverage(r.telemetryMatchedEvents, r.stutterEvents, 60) &&
        coverage(r.gpuMetricCoverageEvents, r.telemetryMatchedEvents, 60) &&
        coverage(r.cpuMetricCoverageEvents, r.telemetryMatchedEvents, 60);
    if (classified >= 5 && covered &&
        coverage(r.telemetryMatchedEvents, r.stutterEvents, 80) &&
        coverage(r.gpuMetricCoverageEvents, r.telemetryMatchedEvents, 80) &&
        coverage(r.cpuMetricCoverageEvents, r.telemetryMatchedEvents, 80)) {
        r.confidence = AutoTuneFeedbackConfidence::High;
    } else if (classified >= 2 && covered) {
        r.confidence = AutoTuneFeedbackConfidence::Medium;
    }
    if (!covered || classified < 2) {
        r.explanation = "Repeated, sufficiently covered CPU/GPU pressure correlations were unavailable. Keep the current profile and collect another representative F9 session.";
        return r;
    }
    if (r.mixedPressureEvents >= 2 || (r.gpuPressureEvents && r.cpuPressureEvents)) {
        r.kind = AutoTuneFeedbackKind::MixedPressure;
        r.explanation = "GPU 3D-engine pressure and single-thread CPU pressure overlap with stutters. A graphics-only preset change is not justified.";
    } else if (r.gpuPressureEvents >= 2 && r.cpuPressureEvents == 0 && r.mixedPressureEvents == 0) {
        r.kind = AutoTuneFeedbackKind::ReduceGraphicsCandidate;
        r.explanation = "Repeated stutters coincide with high per-process GPU 3D-engine utilization and PresentMon GPU-busy evidence. Performance is a candidate for the next controlled comparison; no settings were changed.";
    } else if (r.cpuPressureEvents >= 2 && r.gpuPressureEvents == 0 && r.mixedPressureEvents == 0) {
        r.kind = AutoTuneFeedbackKind::CpuPressure;
        r.explanation = "Repeated stutters coincide with a near-saturated process thread and CPU-busy evidence, without matching GPU 3D pressure. Lowering visual quality is not supported; investigate CPU-side contention.";
    } else {
        r.kind = AutoTuneFeedbackKind::Inconclusive;
        r.explanation = "The observed pressure pattern is not repeated consistently enough to justify a profile change.";
    }
    return r;
}

const char* autoTuneFeedbackKindName(AutoTuneFeedbackKind k) noexcept {
    switch (k) {
    case AutoTuneFeedbackKind::Inconclusive: return "inconclusive";
    case AutoTuneFeedbackKind::ReduceGraphicsCandidate: return "reduce_graphics_candidate";
    case AutoTuneFeedbackKind::CpuPressure: return "cpu_pressure";
    case AutoTuneFeedbackKind::MixedPressure: return "mixed_pressure";
    }
    return "unknown";
}
const char* autoTuneFeedbackConfidenceName(AutoTuneFeedbackConfidence c) noexcept {
    switch (c) {
    case AutoTuneFeedbackConfidence::Low: return "low";
    case AutoTuneFeedbackConfidence::Medium: return "medium";
    case AutoTuneFeedbackConfidence::High: return "high";
    }
    return "unknown";
}

bool writeAutoTuneFeedbackReport(const AutoTuneFeedback& f,
    const std::filesystem::path& jsonPath, const std::filesystem::path& textPath,
    std::wstring* error) {
    AtomicOutputFile jf(jsonPath);
    if (!jf.isOpen()) { if (error) *error = L"Cannot create autotune_feedback.json"; return false; }
    auto& j = jf.stream();
    j << "{\n"
      << "  \"source_valid\": " << (f.sourceValid ? "true" : "false") << ",\n"
      << "  \"analysis_scope\": \"PresentMon and telemetry correlations for stutters >=100 ms\",\n"
      << "  \"automatic_settings_changed\": false,\n"
      << "  \"stutter_events\": " << f.stutterEvents << ",\n"
      << "  \"telemetry_matched_events\": " << f.telemetryMatchedEvents << ",\n"
      << "  \"gpu_3d_metric_coverage_events\": " << f.gpuMetricCoverageEvents << ",\n"
      << "  \"max_thread_cpu_metric_coverage_events\": " << f.cpuMetricCoverageEvents << ",\n"
      << "  \"gpu_pressure_correlated_events\": " << f.gpuPressureEvents << ",\n"
      << "  \"cpu_thread_pressure_correlated_events\": " << f.cpuPressureEvents << ",\n"
      << "  \"mixed_pressure_events\": " << f.mixedPressureEvents << ",\n"
      << "  \"recommendation\": " << std::quoted(autoTuneFeedbackKindName(f.kind)) << ",\n"
      << "  \"evidence_confidence\": " << std::quoted(autoTuneFeedbackConfidenceName(f.confidence)) << ",\n"
      << "  \"explanation\": " << std::quoted(f.explanation) << "\n}\n";
    if (!j || !jf.commit(error)) return false;

    AtomicOutputFile tf(textPath);
    if (!tf.isOpen()) { if (error) *error = L"Cannot create autotune_feedback.txt"; return false; }
    auto& t = tf.stream();
    t << "Bodycam Diagnostics - F9 AutoTune feedback\n"
      << "Automatic settings changed: no\n"
      << "Stutter events: " << f.stutterEvents << "\n"
      << "Telemetry matched: " << f.telemetryMatchedEvents << "\n"
      << "GPU 3D metric coverage: " << f.gpuMetricCoverageEvents << "\n"
      << "Max-thread CPU metric coverage: " << f.cpuMetricCoverageEvents << "\n"
      << "GPU pressure correlations: " << f.gpuPressureEvents << "\n"
      << "CPU thread pressure correlations: " << f.cpuPressureEvents << "\n"
      << "Mixed pressure correlations: " << f.mixedPressureEvents << "\n"
      << "Recommendation: " << autoTuneFeedbackKindName(f.kind) << "\n"
      << "Evidence confidence: " << autoTuneFeedbackConfidenceName(f.confidence) << "\n\n"
      << f.explanation << "\n\n"
      << "Correlation evidence is not proof of root cause or a guaranteed FPS prediction.\n";
    return t && tf.commit(error);
}
