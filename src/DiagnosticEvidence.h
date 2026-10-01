#pragma once
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

struct SourceCapability {
    std::string source;
    std::string status = "not_probed";
    std::uint32_t errorCode = 0;
    bool optional = true;
    std::string detail;
};
struct EnvironmentCapabilities {
    std::vector<SourceCapability> sources;
};
struct WindowsEventEvidence {
    std::string channel;
    std::string provider;
    std::uint32_t eventId = 0;
    std::uint64_t recordId = 0;
    std::int64_t utcUnixMs = 0;
    std::string xml;
};
struct EventLogCollection {
    std::vector<SourceCapability> channels;
    std::vector<WindowsEventEvidence> events;
};
std::string jsonQuoted(const std::string& value);
void writeCapabilitiesJson(std::ostream& out, const EnvironmentCapabilities& profile);
double temporalDistanceMs(double eventMs, double stallStartMs, double durationMs);
bool isTemporalMatch(double eventMs, double stallStartMs, double durationMs, double toleranceMs);
