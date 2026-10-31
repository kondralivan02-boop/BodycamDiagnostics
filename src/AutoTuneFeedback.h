#pragma once
#include "StutterCorrelator.h"
#include <cstddef>
#include <string>

inline constexpr double kAutoTuneMaxTelemetryDeltaMs = 750.0;
enum class AutoTuneFeedbackKind { Inconclusive, ReduceGraphicsCandidate, CpuPressure, MixedPressure };
enum class AutoTuneFeedbackConfidence { Low, Medium, High };
struct AutoTuneFeedback { bool sourceValid=false; std::size_t stutterEvents=0, telemetryMatchedEvents=0, staleTelemetryEvents=0, gpuMetricCoverageEvents=0, cpuMetricCoverageEvents=0, gpuPressureEvents=0, cpuPressureEvents=0, mixedPressureEvents=0; AutoTuneFeedbackKind kind=AutoTuneFeedbackKind::Inconclusive; AutoTuneFeedbackConfidence confidence=AutoTuneFeedbackConfidence::Low; std::string explanation; };
AutoTuneFeedback analyzeAutoTuneFeedback(const StutterCorrelationResult& correlation);
const char* autoTuneFeedbackKindName(AutoTuneFeedbackKind kind) noexcept;
const char* autoTuneFeedbackConfidenceName(AutoTuneFeedbackConfidence confidence) noexcept;
bool writeAutoTuneFeedbackReport(const AutoTuneFeedback&, const std::filesystem::path&, const std::filesystem::path&, std::wstring* error=nullptr);
