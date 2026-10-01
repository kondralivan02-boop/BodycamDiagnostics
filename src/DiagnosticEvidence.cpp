#include "DiagnosticEvidence.h"
#include <cmath>
#include <iomanip>
#include <sstream>

std::string jsonQuoted(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c) << std::dec;
            else out << static_cast<char>(c);
        }
    }
    out << '"';
    return out.str();
}
void writeCapabilitiesJson(std::ostream& out, const EnvironmentCapabilities& profile) {
    out << "{\n  \"schema_version\": 1,\n  \"capture_validity_impact\": false,\n"
        << "  \"note\": \"Observed capabilities only; does not identify Gizmo or prove unrestricted access. started is not a completeness check.\",\n  \"sources\": [\n";
    bool first = true;
    for (const auto& source : profile.sources) {
        if (!first) out << ",\n";
        first = false;
        out << "    {\"source\":" << jsonQuoted(source.source)
            << ",\"status\":" << jsonQuoted(source.status)
            << ",\"win32_error\":" << source.errorCode
            << ",\"optional\":" << (source.optional ? "true" : "false")
            << ",\"detail\":" << jsonQuoted(source.detail) << '}';
    }
    out << "\n  ]\n}\n";
}
double temporalDistanceMs(double eventMs, double startMs, double durationMs) {
    if (eventMs < startMs) return eventMs - startMs;
    if (eventMs > startMs + durationMs) return eventMs - (startMs + durationMs);
    return 0;
}
bool isTemporalMatch(double eventMs, double startMs, double durationMs, double toleranceMs) {
    if (!std::isfinite(eventMs) || !std::isfinite(startMs) || !std::isfinite(durationMs) ||
        !std::isfinite(toleranceMs) || durationMs < 0 || toleranceMs < 0 ||
        !std::isfinite(startMs + durationMs)) return false;
    return std::abs(temporalDistanceMs(eventMs, startMs, durationMs)) <= toleranceMs;
}
