#pragma once
#include "DiagnosticEvidence.h"
#include "StutterCorrelator.h"
#include <chrono>
#include <filesystem>

EnvironmentCapabilities probeEnvironmentCapabilities(std::uint32_t gamePid);
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
