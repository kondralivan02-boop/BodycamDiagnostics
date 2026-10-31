#include "AutoTuneFeedback.h"

#include <cmath>

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
        if (!e.telemetryMatched) continue;
        if (!std::isfinite(e.telemetryDeltaMs) ||
            e.telemetryDeltaMs < 0.0 ||
            e.telemetryDeltaMs > kAutoTuneMaxTelemetryDeltaMs) {
            ++r.staleTelemetryEvents;
            continue;
        }

        ++r.telemetryMatchedEvents;
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
