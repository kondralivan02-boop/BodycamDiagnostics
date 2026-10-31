#include "AutoTuneFeedback.h"
#include "AtomicOutputFile.h"
#include <fstream>
#include <iomanip>

bool writeAutoTuneFeedbackReport(const AutoTuneFeedback& f,
    const std::filesystem::path& jsonPath, const std::filesystem::path& textPath,
    std::wstring* error) {
    AtomicOutputFile jf(jsonPath);
    if (!jf.isOpen()) { if (error) *error = L"Cannot create autotune_feedback.json"; return false; }
    auto& j = jf.stream();
    j << "{\n"
      << "  \"source_valid\": " << (f.sourceValid ? "true" : "false") << ",\n"
      << "  \"schema_version\": 1,\n"
      << "  \"analysis_scope\": \"PresentMon and telemetry correlations for stutters >=100 ms\",\n"
      << "  \"automatic_settings_changed\": false,\n"
      << "  \"confidence_semantics\": \"coverage and repetition tier, not statistical probability\",\n"
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
      << "Evidence confidence is a coverage/repetition tier, not a statistical probability.\n"
      << "Correlation evidence is not proof of root cause or a guaranteed FPS prediction.\n";
    return t && tf.commit(error);
}
