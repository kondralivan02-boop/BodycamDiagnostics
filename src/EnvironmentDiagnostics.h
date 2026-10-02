#pragma once
#include "DiagnosticEvidence.h"
#include "StutterCorrelator.h"
#include <chrono>
#include <filesystem>
#include <cstddef>
#include <string>
#include <vector>

EnvironmentCapabilities probeEnvironmentCapabilities(std::uint32_t gamePid, bool includeEventLogs = true);
bool saveEnvironmentCapabilities(const EnvironmentCapabilities& profile, const std::filesystem::path& output);
EventLogCollection collectWindowsEvents(std::chrono::system_clock::time_point start,
                                       std::chrono::system_clock::time_point end);
bool writeWindowsEventReport(const EventLogCollection& result,
                             const StutterCorrelationResult& stutters,
                             std::chrono::system_clock::time_point presentMonStart,
                             std::chrono::system_clock::time_point sessionStart,
                             std::chrono::system_clock::time_point sessionEnd,
                             bool clockStable, bool gameExited,
                             const std::filesystem::path& output);

struct InfrastructureEvidenceSummary {
    bool instabilityObserved = false;
    std::size_t iscsiEvents = 0;
    std::size_t networkResetEvents = 0;
    std::size_t diskRetryEvents = 0;
    std::size_t dnsTimeoutEvents = 0;
    std::vector<std::string> findings;
};

InfrastructureEvidenceSummary analyzeInfrastructureEvents(
    const EventLogCollection& result);

bool writeInfrastructureEvidenceReport(
    const InfrastructureEvidenceSummary& summary,
    const std::filesystem::path& jsonOutput,
    const std::filesystem::path& textOutput,
    std::wstring* error = nullptr);
