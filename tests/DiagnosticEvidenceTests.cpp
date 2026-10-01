#include "DiagnosticEvidence.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    check(temporalDistanceMs(9500, 10000, 500) == -500, "before stall");
    check(temporalDistanceMs(10400, 10000, 500) == 0, "within stall");
    check(temporalDistanceMs(12000, 10000, 5000) == 0, "long stall interval");
    check(temporalDistanceMs(10900, 10000, 500) == 400, "after stall end");
    check(isTemporalMatch(12500, 10000, 500, 2000), "inclusive boundary");
    check(!isTemporalMatch(12501, 10000, 500, 2000), "outside boundary");
    check(!isTemporalMatch(10000, 10000, -1, 2000), "reject negative duration");
    check(!isTemporalMatch(10000, std::numeric_limits<double>::quiet_NaN(), 5, 2000), "reject nonfinite input");
    EnvironmentCapabilities caps;
    caps.sources.push_back({"event_log_system", "access_denied", 5, true, "current token"});
    caps.sources.push_back({"event_log_application", "available_empty", 0, true, "read succeeded"});
    std::ostringstream json;
    writeCapabilitiesJson(json, caps);
    check(json.str().find("access_denied") != std::string::npos, "retain denied source");
    check(json.str().find("available_empty") != std::string::npos, "empty is readable");
    check(json.str().find("\"capture_validity_impact\": false") != std::string::npos, "optional capabilities do not invalidate");
    check(jsonQuoted("a\"\n\x01") == "\"a\\\"\\n\\u0001\"", "escape all JSON control characters");
    std::cout << "Diagnostic evidence tests passed\n";
}
