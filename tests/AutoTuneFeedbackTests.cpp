#include "AutoTuneFeedback.h"
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
StutterEvent makeEvent(bool gpu, bool cpu) {
    StutterEvent e{};
    e.telemetryMatched = true;
    e.telemetryDeltaMs = 250.0;
    e.frameTimeMs = 150.0;
    e.gpuBusyMs = gpu ? 120.0 : 0.0;
    e.cpuBusyMs = cpu ? 120.0 : 0.0;
    e.processGpu3dValid = true;
    e.processGpu3dMaxPct = gpu ? 96.0 : 35.0;
    e.processMaxThreadCpuValid = true;
    e.processMaxThreadCpuPct = cpu ? 97.0 : 42.0;
    return e;
}
}
int main() {
    StutterCorrelationResult gpu;
    gpu.valid = true;
    gpu.events = {makeEvent(true, false), makeEvent(true, false), makeEvent(true, false)};
    const auto gr = analyzeAutoTuneFeedback(gpu);
    check(gr.kind == AutoTuneFeedbackKind::ReduceGraphicsCandidate, "GPU pressure candidate");
    check(gr.confidence == AutoTuneFeedbackConfidence::Medium, "medium evidence confidence");

    StutterCorrelationResult cpu;
    cpu.valid = true;
    cpu.events = {makeEvent(false, true), makeEvent(false, true)};
    check(analyzeAutoTuneFeedback(cpu).kind == AutoTuneFeedbackKind::CpuPressure,
          "CPU pressure must not lower graphics");

    StutterCorrelationResult mixed;
    mixed.valid = true;
    mixed.events = {makeEvent(true, true), makeEvent(true, true)};
    check(analyzeAutoTuneFeedback(mixed).kind == AutoTuneFeedbackKind::MixedPressure,
          "mixed CPU/GPU pressure");

    StutterCorrelationResult missing;
    missing.valid = true;
    auto unavailable = makeEvent(true, false);
    unavailable.processGpu3dValid = false;
    unavailable.processMaxThreadCpuValid = false;
    missing.events = {unavailable, unavailable};
    check(analyzeAutoTuneFeedback(missing).kind == AutoTuneFeedbackKind::Inconclusive,
          "missing telemetry remains inconclusive");

    StutterCorrelationResult sparse;
    sparse.valid = true;
    auto noTelemetry = makeEvent(false, false);
    noTelemetry.telemetryMatched = false;
    noTelemetry.processGpu3dValid = false;
    noTelemetry.processMaxThreadCpuValid = false;
    sparse.events = {makeEvent(true, false), makeEvent(true, false),
                     noTelemetry, noTelemetry, noTelemetry};
    check(analyzeAutoTuneFeedback(sparse).kind == AutoTuneFeedbackKind::Inconclusive,
          "sparse telemetry matching must not trigger a profile recommendation");

    StutterCorrelationResult freshnessBoundary;
    freshnessBoundary.valid = true;
    auto boundaryA = makeEvent(true, false);
    auto boundaryB = makeEvent(true, false);
    boundaryA.telemetryDeltaMs = kAutoTuneMaxTelemetryDeltaMs;
    boundaryB.telemetryDeltaMs = kAutoTuneMaxTelemetryDeltaMs;
    freshnessBoundary.events = {boundaryA, boundaryB};
    check(analyzeAutoTuneFeedback(freshnessBoundary).kind == AutoTuneFeedbackKind::ReduceGraphicsCandidate,
          "telemetry exactly at the freshness cutoff remains eligible");

    StutterCorrelationResult stale;
    stale.valid = true;
    auto staleA = makeEvent(true, false);
    auto staleB = makeEvent(true, false);
    staleA.telemetryDeltaMs = kAutoTuneMaxTelemetryDeltaMs + 1.0;
    staleB.telemetryDeltaMs = kAutoTuneMaxTelemetryDeltaMs + 1.0;
    stale.events = {staleA, staleB};
    const auto staleResult = analyzeAutoTuneFeedback(stale);
    check(staleResult.kind == AutoTuneFeedbackKind::Inconclusive,
          "stale telemetry must not trigger a profile recommendation");
    check(staleResult.telemetryMatchedEvents == 0 && staleResult.staleTelemetryEvents == 2,
          "stale samples must be counted as excluded, not fresh matches");

    StutterCorrelationResult clean;
    clean.valid = true;
    check(analyzeAutoTuneFeedback(clean).kind == AutoTuneFeedbackKind::Inconclusive,
          "no stutters means no automatic adjustment");
    std::cout << "AutoTuneFeedbackTests PASS\n";
}
