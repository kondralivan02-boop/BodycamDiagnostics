#include "StutterCorrelator.h"

#include <Windows.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
std::vector<std::string> parseCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];

        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field.push_back(c);
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            fields.push_back(std::move(field));
            field.clear();
        } else if (c != '\r') {
            field.push_back(c);
        }
    }

    fields.push_back(std::move(field));
    return fields;
}

std::string trimBom(std::string value) {
    if (value.size() >= 3 &&
        static_cast<unsigned char>(value[0]) == 0xEF &&
        static_cast<unsigned char>(value[1]) == 0xBB &&
        static_cast<unsigned char>(value[2]) == 0xBF) {
        value.erase(0, 3);
    }
    return value;
}

std::string utf16LeBytesToUtf8(const std::string& bytes) {
    if (bytes.empty())
        return {};

    const std::size_t charCount = bytes.size() / 2;
    std::wstring wide(charCount, L'\0');

    for (std::size_t i = 0; i < charCount; ++i) {
        const auto lo = static_cast<unsigned char>(bytes[i * 2]);
        const auto hi = static_cast<unsigned char>(bytes[i * 2 + 1]);
        wide[i] = static_cast<wchar_t>(
            static_cast<unsigned>(lo) |
            (static_cast<unsigned>(hi) << 8));
    }

    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.data(),
        static_cast<int>(wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (required <= 0)
        return {};

    std::string out(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.data(),
        static_cast<int>(wide.size()),
        out.data(),
        required,
        nullptr,
        nullptr);

    return out;
}

bool loadTextUtf8(const std::filesystem::path& path, std::string& text) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;

    std::string bytes(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());

    if (bytes.size() >= 2 &&
        static_cast<unsigned char>(bytes[0]) == 0xFF &&
        static_cast<unsigned char>(bytes[1]) == 0xFE) {
        bytes.erase(0, 2);
        if (bytes.size() % 2 != 0)
            bytes.pop_back();
        text = utf16LeBytesToUtf8(bytes);
        return true;
    }

    text = trimBom(std::move(bytes));
    return true;
}

std::size_t findColumn(
    const std::vector<std::string>& headers,
    const char* name) {

    const auto it = std::find(headers.begin(), headers.end(), name);
    if (it == headers.end())
        return std::numeric_limits<std::size_t>::max();

    return static_cast<std::size_t>(std::distance(headers.begin(), it));
}

bool parseDouble(
    const std::vector<std::string>& fields,
    std::size_t index,
    double& value) {

    if (index >= fields.size())
        return false;

    const auto& text = fields[index];
    if (text.empty() || text == "NA")
        return false;

    char* end = nullptr;
    errno = 0;

    const double parsed = std::strtod(text.c_str(), &end);
    if (errno != 0 ||
        end == text.c_str() ||
        *end != '\0' ||
        !std::isfinite(parsed)) {
        return false;
    }

    value = parsed;
    return true;
}

bool parseTimestamp100ns(const std::string& text, unsigned long long& value) {
    const auto dot = text.find('.');
    const std::string base =
        dot == std::string::npos ? text : text.substr(0, dot);

    unsigned year = 0;
    unsigned month = 0;
    unsigned day = 0;
    unsigned hour = 0;
    unsigned minute = 0;
    unsigned second = 0;

    if (sscanf_s(
            base.c_str(),
            "%u-%u-%u %u:%u:%u",
            &year,
            &month,
            &day,
            &hour,
            &minute,
            &second) != 6) {
        return false;
    }

    unsigned milliseconds = 0;
    if (dot != std::string::npos && dot + 1 < text.size()) {
        std::string fraction = text.substr(dot + 1);
        if (fraction.size() > 3)
            fraction.resize(3);
        while (fraction.size() < 3)
            fraction.push_back('0');
        milliseconds = static_cast<unsigned>(
            std::strtoul(fraction.c_str(), nullptr, 10));
    }

    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(year);
    st.wMonth = static_cast<WORD>(month);
    st.wDay = static_cast<WORD>(day);
    st.wHour = static_cast<WORD>(hour);
    st.wMinute = static_cast<WORD>(minute);
    st.wSecond = static_cast<WORD>(second);
    st.wMilliseconds = static_cast<WORD>(milliseconds);

    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft))
        return false;

    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    value = u.QuadPart;
    return true;
}

std::string jsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);

    for (const char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c); break;
        }
    }

    return out;
}

std::string formatVideoBookmark(double valueMs) {
    if (!std::isfinite(valueMs) || valueMs < 0.0)
        return "unavailable";

    const auto totalMs = static_cast<long long>(std::llround(valueMs));
    const auto hours = totalMs / 3600000;
    const auto minutes = (totalMs / 60000) % 60;
    const auto seconds = (totalMs / 1000) % 60;
    const auto milliseconds = totalMs % 1000;

    std::ostringstream out;
    if (hours > 0)
        out << hours << ":";

    out << std::setfill('0')
        << std::setw(2) << minutes << ":"
        << std::setw(2) << seconds << "."
        << std::setw(3) << milliseconds;
    return out.str();
}

struct TelemetryRow {
    unsigned long long time100ns = 0;
    double processCpuPct = 0.0;
    double systemCpuPct = 0.0;
    double workingSetMiB = 0.0;
    double privateUsageMiB = 0.0;
    double processReadMiBps = 0.0;
    double processWriteMiBps = 0.0;
    double systemMemoryLoadPct = 0.0;
    double systemMemoryAvailableMiB = 0.0;
    double gpuUtilPct = 0.0;
    double gpuMemoryUtilPct = 0.0;
    double vramUsedMiB = 0.0;
    double vramTotalMiB = 0.0;
    double gpuTempC = 0.0;
    double gpuGraphicsMHz = 0.0;
    double gpuMemoryMHz = 0.0;
    double gpuPowerW = 0.0;
    double gpuPowerLimitW = 0.0;
};

bool loadTelemetry(
    const std::filesystem::path& telemetryCsv,
    std::vector<TelemetryRow>& rows) {

    std::string csvText;
    if (!loadTextUtf8(telemetryCsv, csvText))
        return false;

    std::istringstream in(csvText);
    std::string headerLine;
    if (!std::getline(in, headerLine))
        return false;

    const auto headers = parseCsvLine(trimBom(headerLine));
    const auto timestampIndex = findColumn(headers, "timestamp");
    if (timestampIndex == std::numeric_limits<std::size_t>::max())
        return false;

    const auto processCpuIndex = findColumn(headers, "process_cpu_pct");
    const auto systemCpuIndex = findColumn(headers, "system_cpu_pct");
    const auto workingSetIndex = findColumn(headers, "working_set_mib");
    const auto privateUsageIndex = findColumn(headers, "private_usage_mib");
    const auto processReadIndex = findColumn(headers, "process_read_mib_s");
    const auto processWriteIndex = findColumn(headers, "process_write_mib_s");
    const auto memoryLoadIndex = findColumn(headers, "system_memory_load_pct");
    const auto memoryAvailableIndex = findColumn(headers, "system_memory_available_mib");
    const auto gpuUtilIndex = findColumn(headers, "gpu_util_pct");
    const auto gpuMemoryUtilIndex = findColumn(headers, "gpu_mem_util_pct");
    const auto vramUsedIndex = findColumn(headers, "vram_used_mib");
    const auto vramTotalIndex = findColumn(headers, "vram_total_mib");
    const auto gpuTempIndex = findColumn(headers, "gpu_temp_c");
    const auto gpuGraphicsIndex = findColumn(headers, "gpu_graphics_mhz");
    const auto gpuMemoryIndex = findColumn(headers, "gpu_memory_mhz");
    const auto gpuPowerIndex = findColumn(headers, "gpu_power_w");
    const auto gpuPowerLimitIndex = findColumn(headers, "gpu_power_limit_w");

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;

        const auto fields = parseCsvLine(line);
        if (timestampIndex >= fields.size())
            continue;

        TelemetryRow row{};
        if (!parseTimestamp100ns(fields[timestampIndex], row.time100ns))
            continue;

        parseDouble(fields, processCpuIndex, row.processCpuPct);
        parseDouble(fields, systemCpuIndex, row.systemCpuPct);
        parseDouble(fields, workingSetIndex, row.workingSetMiB);
        parseDouble(fields, privateUsageIndex, row.privateUsageMiB);
        parseDouble(fields, processReadIndex, row.processReadMiBps);
        parseDouble(fields, processWriteIndex, row.processWriteMiBps);
        parseDouble(fields, memoryLoadIndex, row.systemMemoryLoadPct);
        parseDouble(fields, memoryAvailableIndex, row.systemMemoryAvailableMiB);
        parseDouble(fields, gpuUtilIndex, row.gpuUtilPct);
        parseDouble(fields, gpuMemoryUtilIndex, row.gpuMemoryUtilPct);
        parseDouble(fields, vramUsedIndex, row.vramUsedMiB);
        parseDouble(fields, vramTotalIndex, row.vramTotalMiB);
        parseDouble(fields, gpuTempIndex, row.gpuTempC);
        parseDouble(fields, gpuGraphicsIndex, row.gpuGraphicsMHz);
        parseDouble(fields, gpuMemoryIndex, row.gpuMemoryMHz);
        parseDouble(fields, gpuPowerIndex, row.gpuPowerW);
        parseDouble(fields, gpuPowerLimitIndex, row.gpuPowerLimitW);
        rows.push_back(row);
    }

    std::sort(
        rows.begin(),
        rows.end(),
        [](const TelemetryRow& a, const TelemetryRow& b) {
            return a.time100ns < b.time100ns;
        });

    return !rows.empty();
}

const TelemetryRow* nearestTelemetry(
    const std::vector<TelemetryRow>& rows,
    unsigned long long time100ns,
    double& deltaMs) {

    if (rows.empty())
        return nullptr;

    const auto it = std::lower_bound(
        rows.begin(),
        rows.end(),
        time100ns,
        [](const TelemetryRow& row, unsigned long long value) {
            return row.time100ns < value;
        });

    const TelemetryRow* best = nullptr;
    unsigned long long bestDelta =
        std::numeric_limits<unsigned long long>::max();

    auto consider = [&](const TelemetryRow& row) {
        const unsigned long long delta =
            row.time100ns > time100ns
                ? row.time100ns - time100ns
                : time100ns - row.time100ns;

        if (delta < bestDelta) {
            bestDelta = delta;
            best = &row;
        }
    };

    if (it != rows.end())
        consider(*it);
    if (it != rows.begin())
        consider(*std::prev(it));
    if (!best)
        return nullptr;

    deltaMs = static_cast<double>(bestDelta) / 10000.0;
    return best;
}

StutterTelemetryPoint makeTelemetryPoint(
    const TelemetryRow& row,
    unsigned long long eventStartTime100ns) {

    StutterTelemetryPoint point{};
    point.valid = true;

    const long long signedDelta =
        row.time100ns >= eventStartTime100ns
            ? static_cast<long long>(row.time100ns - eventStartTime100ns)
            : -static_cast<long long>(eventStartTime100ns - row.time100ns);

    point.offsetMs = static_cast<double>(signedDelta) / 10000.0;
    point.processCpuPct = row.processCpuPct;
    point.systemCpuPct = row.systemCpuPct;
    point.workingSetMiB = row.workingSetMiB;
    point.privateUsageMiB = row.privateUsageMiB;
    point.processReadMiBps = row.processReadMiBps;
    point.processWriteMiBps = row.processWriteMiBps;
    point.systemMemoryLoadPct = row.systemMemoryLoadPct;
    point.systemMemoryAvailableMiB = row.systemMemoryAvailableMiB;
    point.gpuUtilPct = row.gpuUtilPct;
    point.gpuMemoryUtilPct = row.gpuMemoryUtilPct;
    point.vramUsedMiB = row.vramUsedMiB;
    point.vramTotalMiB = row.vramTotalMiB;
    point.gpuTempC = row.gpuTempC;
    point.gpuGraphicsMHz = row.gpuGraphicsMHz;
    point.gpuMemoryMHz = row.gpuMemoryMHz;
    point.gpuPowerW = row.gpuPowerW;
    point.gpuPowerLimitW = row.gpuPowerLimitW;
    return point;
}

void copyTelemetry(
    StutterEvent& event,
    const TelemetryRow& row,
    double deltaMs) {

    event.telemetryMatched = true;
    event.telemetryDeltaMs = deltaMs;
    event.processCpuPct = row.processCpuPct;
    event.systemCpuPct = row.systemCpuPct;
    event.workingSetMiB = row.workingSetMiB;
    event.privateUsageMiB = row.privateUsageMiB;
    event.processReadMiBps = row.processReadMiBps;
    event.processWriteMiBps = row.processWriteMiBps;
    event.systemMemoryLoadPct = row.systemMemoryLoadPct;
    event.systemMemoryAvailableMiB = row.systemMemoryAvailableMiB;
    event.gpuUtilPct = row.gpuUtilPct;
    event.gpuMemoryUtilPct = row.gpuMemoryUtilPct;
    event.vramUsedMiB = row.vramUsedMiB;
    event.vramTotalMiB = row.vramTotalMiB;
    event.gpuTempC = row.gpuTempC;
    event.gpuGraphicsMHz = row.gpuGraphicsMHz;
    event.gpuMemoryMHz = row.gpuMemoryMHz;
    event.gpuPowerW = row.gpuPowerW;
    event.gpuPowerLimitW = row.gpuPowerLimitW;
}

StutterTelemetryDelta telemetryDelta(
    const StutterTelemetryPoint& from,
    const StutterTelemetryPoint& to) {

    StutterTelemetryDelta delta{};
    if (!from.valid || !to.valid)
        return delta;

    delta.valid = true;
    delta.processCpuPct = to.processCpuPct - from.processCpuPct;
    delta.systemCpuPct = to.systemCpuPct - from.systemCpuPct;
    delta.processReadMiBps = to.processReadMiBps - from.processReadMiBps;
    delta.processWriteMiBps = to.processWriteMiBps - from.processWriteMiBps;
    delta.gpuUtilPct = to.gpuUtilPct - from.gpuUtilPct;
    delta.gpuGraphicsMHz = to.gpuGraphicsMHz - from.gpuGraphicsMHz;
    delta.gpuPowerW = to.gpuPowerW - from.gpuPowerW;
    return delta;
}

void calculateTelemetryDeltas(StutterEvent& event) {
    event.beforeToNearest = telemetryDelta(event.before, event.nearest);
    event.nearestToAfter = telemetryDelta(event.nearest, event.after);
    event.beforeToAfter = telemetryDelta(event.before, event.after);
}

void attachTelemetryWindow(
    StutterEvent& event,
    const std::vector<TelemetryRow>& rows,
    unsigned long long eventStartTime100ns,
    unsigned long long eventEndTime100ns) {

    if (rows.empty())
        return;

    const auto startIt = std::lower_bound(
        rows.begin(),
        rows.end(),
        eventStartTime100ns,
        [](const TelemetryRow& row, unsigned long long value) {
            return row.time100ns < value;
        });

    if (startIt != rows.begin()) {
        const auto& row = *std::prev(startIt);
        const double beforeDeltaMs =
            static_cast<double>(eventStartTime100ns - row.time100ns) / 10000.0;
        if (beforeDeltaMs <= 2500.0)
            event.before = makeTelemetryPoint(row, eventStartTime100ns);
    }

    const auto afterIt = std::lower_bound(
        rows.begin(),
        rows.end(),
        eventEndTime100ns,
        [](const TelemetryRow& row, unsigned long long value) {
            return row.time100ns < value;
        });

    if (afterIt != rows.end()) {
        const double afterDeltaMs =
            static_cast<double>(afterIt->time100ns - eventEndTime100ns) / 10000.0;
        if (afterDeltaMs <= 2500.0)
            event.after = makeTelemetryPoint(*afterIt, eventStartTime100ns);
    }

    double nearestDeltaMs = 0.0;
    if (const auto* row = nearestTelemetry(rows, eventStartTime100ns, nearestDeltaMs)) {
        if (nearestDeltaMs <= 1500.0) {
            event.nearest = makeTelemetryPoint(*row, eventStartTime100ns);
            copyTelemetry(event, *row, nearestDeltaMs);
        }
    }
}

void classifySignals(StutterEvent& event) {
    const double frame = event.frameTimeMs;

    if (event.cpuBusyMs >= 100.0 && event.cpuBusyMs >= frame * 0.50)
        event.signals.push_back("CPU_BUSY_DOMINANT");

    if (event.cpuWaitMs >= 100.0 && event.cpuWaitMs >= frame * 0.25)
        event.signals.push_back("CPU_WAIT_HIGH");

    if (event.gpuBusyMs >= 100.0 && event.gpuBusyMs >= frame * 0.25)
        event.signals.push_back("GPU_BUSY_HIGH");

    if (event.gpuWaitMs >= 100.0 && event.gpuWaitMs >= frame * 0.25)
        event.signals.push_back("GPU_WAIT_HIGH");

    if (event.beforeToNearest.valid && event.beforeToNearest.gpuUtilPct <= -30.0)
        event.signals.push_back("GPU_UTIL_DROP_AROUND_STALL");

    if (event.beforeToNearest.valid && event.beforeToNearest.gpuGraphicsMHz <= -500.0)
        event.signals.push_back("GPU_CLOCK_DROP_AROUND_STALL");

    if (event.nearestToAfter.valid && event.nearestToAfter.gpuUtilPct >= 30.0)
        event.signals.push_back("GPU_UTIL_RECOVERY_AFTER_STALL");

    if (event.nearestToAfter.valid && event.nearestToAfter.gpuGraphicsMHz >= 500.0)
        event.signals.push_back("GPU_CLOCK_RECOVERY_AFTER_STALL");

    if (event.beforeToNearest.valid && event.beforeToNearest.processReadMiBps >= 50.0)
        event.signals.push_back("BODYCAM_READ_SPIKE_AROUND_STALL");

    if (event.beforeToNearest.valid && event.beforeToNearest.processWriteMiBps >= 50.0)
        event.signals.push_back("BODYCAM_WRITE_SPIKE_AROUND_STALL");

    if (!event.telemetryMatched)
        return;

    if (frame >= 250.0 && event.gpuUtilPct <= 15.0)
        event.signals.push_back("GPU_UTIL_LOW_NEAR_STALL");

    if (event.systemCpuPct >= 90.0)
        event.signals.push_back("SYSTEM_CPU_HIGH");

    if (event.systemMemoryLoadPct >= 90.0 ||
        (event.systemMemoryAvailableMiB > 0.0 &&
         event.systemMemoryAvailableMiB <= 2048.0)) {
        event.signals.push_back("SYSTEM_MEMORY_PRESSURE");
    }

    if (event.vramTotalMiB > 0.0 &&
        event.vramUsedMiB / event.vramTotalMiB >= 0.95) {
        event.signals.push_back("VRAM_PRESSURE");
    }

    if (event.gpuPowerLimitW > 0.0 &&
        event.gpuPowerW / event.gpuPowerLimitW >= 0.95) {
        event.signals.push_back("GPU_POWER_NEAR_LIMIT");
    }
}

void writeTelemetryPointJson(
    std::ofstream& json,
    const StutterTelemetryPoint& p) {

    if (!p.valid) {
        json << "null";
        return;
    }

    json << "{"
         << "\"offset_ms\":" << p.offsetMs
         << ",\"process_cpu_pct\":" << p.processCpuPct
         << ",\"system_cpu_pct\":" << p.systemCpuPct
         << ",\"working_set_mib\":" << p.workingSetMiB
         << ",\"private_usage_mib\":" << p.privateUsageMiB
         << ",\"process_read_mib_s\":" << p.processReadMiBps
         << ",\"process_write_mib_s\":" << p.processWriteMiBps
         << ",\"system_memory_load_pct\":" << p.systemMemoryLoadPct
         << ",\"system_memory_available_mib\":" << p.systemMemoryAvailableMiB
         << ",\"gpu_util_pct\":" << p.gpuUtilPct
         << ",\"gpu_mem_util_pct\":" << p.gpuMemoryUtilPct
         << ",\"vram_used_mib\":" << p.vramUsedMiB
         << ",\"vram_total_mib\":" << p.vramTotalMiB
         << ",\"gpu_temp_c\":" << p.gpuTempC
         << ",\"gpu_graphics_mhz\":" << p.gpuGraphicsMHz
         << ",\"gpu_memory_mhz\":" << p.gpuMemoryMHz
         << ",\"gpu_power_w\":" << p.gpuPowerW
         << ",\"gpu_power_limit_w\":" << p.gpuPowerLimitW
         << "}";
}

void writeTelemetryDeltaJson(
    std::ofstream& json,
    const StutterTelemetryDelta& d) {

    if (!d.valid) {
        json << "null";
        return;
    }

    json << "{"
         << "\"process_cpu_pct\":" << d.processCpuPct
         << ",\"system_cpu_pct\":" << d.systemCpuPct
         << ",\"process_read_mib_s\":" << d.processReadMiBps
         << ",\"process_write_mib_s\":" << d.processWriteMiBps
         << ",\"gpu_util_pct\":" << d.gpuUtilPct
         << ",\"gpu_graphics_mhz\":" << d.gpuGraphicsMHz
         << ",\"gpu_power_w\":" << d.gpuPowerW
         << "}";
}

void writePointText(
    std::ofstream& text,
    const StutterTelemetryPoint& p) {

    if (!p.valid) {
        text << "none";
        return;
    }

    text << p.offsetMs << " ms, GPU "
         << p.gpuUtilPct << " %, CPU "
         << p.processCpuPct << " %, IO "
         << p.processReadMiBps << "/"
         << p.processWriteMiBps << " MiB/s";
}
}

void classifyStutterIndicators(StutterEvent& event) {
    const auto addUnique = [&](const char* indicator) {
        if (std::find(event.diagnosticIndicators.begin(), event.diagnosticIndicators.end(), indicator) ==
            event.diagnosticIndicators.end()) {
            event.diagnosticIndicators.emplace_back(indicator);
        }
    };

    const double frame = event.frameTimeMs;
    const bool cpuBusyDominant =
        event.cpuBusyMs >= 100.0 &&
        event.cpuBusyMs >= frame * 0.50;
    const bool cpuWaitHigh =
        event.cpuWaitMs >= 100.0 &&
        event.cpuWaitMs >= frame * 0.25;
    const bool gpuBusyHigh =
        event.gpuBusyMs >= 100.0 &&
        event.gpuBusyMs >= frame * 0.25;
    const bool gpuWaitHigh =
        event.gpuWaitMs >= 100.0 &&
        event.gpuWaitMs >= frame * 0.25;
    const bool gpuLowNearStall =
        event.telemetryMatched &&
        frame >= 250.0 &&
        event.gpuUtilPct <= 15.0;
    const bool ioSpike =
        event.beforeToNearest.valid &&
        (event.beforeToNearest.processReadMiBps >= 50.0 ||
         event.beforeToNearest.processWriteMiBps >= 50.0);
    const bool memoryPressure =
        event.telemetryMatched &&
        (event.systemMemoryLoadPct >= 90.0 ||
         (event.systemMemoryAvailableMiB > 0.0 &&
          event.systemMemoryAvailableMiB <= 2048.0));
    const bool vramPressure =
        event.telemetryMatched &&
        event.vramTotalMiB > 0.0 &&
        event.vramUsedMiB / event.vramTotalMiB >= 0.95;

    if (cpuBusyDominant || cpuWaitHigh)
        addUnique("CPU_STALL_INDICATOR");

    if (gpuBusyHigh)
        addUnique("GPU_BUSY_STALL_INDICATOR");

    if (gpuLowNearStall && !gpuBusyHigh)
        addUnique("GPU_IDLE_STALL_INDICATOR");

    if (ioSpike)
        addUnique("IO_SPIKE_INDICATOR");

    if (memoryPressure)
        addUnique("MEMORY_PRESSURE_INDICATOR");

    if (vramPressure)
        addUnique("VRAM_PRESSURE_INDICATOR");

    if (event.presentModeChanged)
        addUnique("PRESENT_MODE_CHANGE_INDICATOR");

    if (gpuWaitHigh && gpuLowNearStall && !gpuBusyHigh)
        addUnique("POSSIBLE_DRIVER_RENDER_PATH_STALL");
}

StutterCorrelationResult correlateStutterEvents(
    const std::filesystem::path& presentMonCsv,
    const std::filesystem::path& telemetryCsv,
    double thresholdMs,
    bool videoBookmarkAlignmentAvailable,
    double videoStartAfterPresentMonMs) {

    StutterCorrelationResult result;
    result.videoBookmarkAlignmentAvailable =
        videoBookmarkAlignmentAvailable &&
        std::isfinite(videoStartAfterPresentMonMs) &&
        videoStartAfterPresentMonMs >= 0.0;
    result.videoStartAfterPresentMonMs =
        result.videoBookmarkAlignmentAvailable
            ? videoStartAfterPresentMonMs
            : 0.0;

    std::string csvText;

    if (!loadTextUtf8(presentMonCsv, csvText)) {
        result.error = L"Cannot open presentmon.csv";
        return result;
    }

    std::istringstream in(csvText);
    std::string headerLine;
    if (!std::getline(in, headerLine)) {
        result.error = L"presentmon.csv is empty";
        return result;
    }

    const auto headers = parseCsvLine(trimBom(headerLine));
    const auto timeIndex = findColumn(headers, "CPUStartDateTime");
    const auto frameTimeIndex = findColumn(headers, "FrameTime");
    const auto cpuBusyIndex = findColumn(headers, "CPUBusy");
    const auto cpuWaitIndex = findColumn(headers, "CPUWait");
    const auto gpuTimeIndex = findColumn(headers, "GPUTime");
    const auto gpuBusyIndex = findColumn(headers, "GPUBusy");
    const auto gpuWaitIndex = findColumn(headers, "GPUWait");
    const auto presentModeIndex = findColumn(headers, "PresentMode");

    if (timeIndex == std::numeric_limits<std::size_t>::max() ||
        frameTimeIndex == std::numeric_limits<std::size_t>::max()) {
        result.error = L"presentmon.csv lacks CPUStartDateTime or FrameTime";
        return result;
    }

    std::vector<TelemetryRow> telemetryRows;
    loadTelemetry(telemetryCsv, telemetryRows);

    std::string line;
    std::string previousPresentMode;
    unsigned long long presentMonBaseTime100ns = 0;
    bool presentMonBaseSet = false;

    while (std::getline(in, line)) {
        if (line.empty())
            continue;

        const auto fields = parseCsvLine(line);
        if (timeIndex >= fields.size())
            continue;

        unsigned long long eventTime100ns = 0;
        if (!parseTimestamp100ns(fields[timeIndex], eventTime100ns))
            continue;

        if (!presentMonBaseSet) {
            presentMonBaseTime100ns = eventTime100ns;
            presentMonBaseSet = true;
        }

        std::string currentPresentMode;
        if (presentModeIndex < fields.size())
            currentPresentMode = fields[presentModeIndex];

        double frameTimeMs = 0.0;
        if (!parseDouble(fields, frameTimeIndex, frameTimeMs)) {
            if (!currentPresentMode.empty())
                previousPresentMode = currentPresentMode;
            continue;
        }

        if (frameTimeMs < thresholdMs) {
            if (!currentPresentMode.empty())
                previousPresentMode = currentPresentMode;
            continue;
        }

        StutterEvent event{};
        event.timestamp = fields[timeIndex];
        event.relativeTimeValid = eventTime100ns >= presentMonBaseTime100ns;
        if (event.relativeTimeValid) {
            event.relativeTimeMs =
                static_cast<double>(eventTime100ns - presentMonBaseTime100ns) /
                10000.0;

            if (result.videoBookmarkAlignmentAvailable) {
                const double bookmarkMs =
                    event.relativeTimeMs -
                    result.videoStartAfterPresentMonMs;

                if (std::isfinite(bookmarkMs) && bookmarkMs >= 0.0) {
                    event.videoBookmarkValid = true;
                    event.videoBookmarkMs = bookmarkMs;
                }
            }
        }
        event.frameTimeMs = frameTimeMs;
        parseDouble(fields, cpuBusyIndex, event.cpuBusyMs);
        parseDouble(fields, cpuWaitIndex, event.cpuWaitMs);
        parseDouble(fields, gpuTimeIndex, event.gpuTimeMs);
        parseDouble(fields, gpuBusyIndex, event.gpuBusyMs);
        parseDouble(fields, gpuWaitIndex, event.gpuWaitMs);

        event.presentMode = currentPresentMode;
        event.previousPresentMode = previousPresentMode;
        event.presentModeChanged =
            !event.presentMode.empty() &&
            !event.previousPresentMode.empty() &&
            event.presentMode != event.previousPresentMode;

        unsigned long long telemetryStartTime100ns = eventTime100ns;
        if (!telemetryRows.empty() &&
            presentMonBaseSet &&
            eventTime100ns >= presentMonBaseTime100ns) {
            telemetryStartTime100ns =
                telemetryRows.front().time100ns +
                (eventTime100ns - presentMonBaseTime100ns);
        }

        const unsigned long long frameDuration100ns =
            static_cast<unsigned long long>(
                std::llround(std::max(0.0, frameTimeMs) * 10000.0));
        const unsigned long long telemetryEndTime100ns =
            telemetryStartTime100ns + frameDuration100ns;

        attachTelemetryWindow(
            event,
            telemetryRows,
            telemetryStartTime100ns,
            telemetryEndTime100ns);

        calculateTelemetryDeltas(event);
        classifySignals(event);
        classifyStutterIndicators(event);
        result.events.push_back(std::move(event));

        if (!currentPresentMode.empty())
            previousPresentMode = currentPresentMode;
    }

    result.valid = true;
    return result;
}

bool writeStutterCorrelationReport(
    const StutterCorrelationResult& result,
    const std::filesystem::path& jsonOutput,
    const std::filesystem::path& textOutput,
    std::wstring* error) {

    if (!result.valid) {
        if (error)
            *error = result.error.empty()
                ? L"Stutter correlation is invalid"
                : result.error;
        return false;
    }

    std::ofstream json(jsonOutput, std::ios::binary | std::ios::trunc);
    if (!json) {
        if (error)
            *error = L"Cannot create stutter_events.json";
        return false;
    }

    json << std::fixed << std::setprecision(3);
    json << "{\n";
    json << "  \"threshold_ms\": 100.0,\n";
    json << "  \"event_count\": " << result.events.size() << ",\n";
    json << "  \"telemetry_window_semantics\": "
            "\"before=start-previous; nearest=start-nearest; after=end-next\",\n";
    json << "  \"video_bookmark_alignment_available\": "
         << (result.videoBookmarkAlignmentAvailable ? "true" : "false")
         << ",\n";
    json << "  \"video_start_after_presentmon_ms\": "
         << result.videoStartAfterPresentMonMs
         << ",\n";
    json << "  \"video_bookmark_alignment\": "
            "\"estimated first-PresentMon-frame-relative timeline minus observed PresentMon-launch-to-FFmpeg-start delay\",\n";
    json << "  \"video_bookmark_note\": "
            "\"Bookmarks are navigation estimates, not frame-accurate synchronization. PresentMon first-frame startup delay and FFmpeg startup latency are not measured directly; bookmarks are disabled after an observed recorder restart or unstable wall clock.\",\n";
    json << "  \"events\": [\n";

    for (std::size_t i = 0; i < result.events.size(); ++i) {
        const auto& e = result.events[i];

        json << "    {\n";
        json << "      \"timestamp\": \"" << jsonEscape(e.timestamp) << "\",\n";
        json << "      \"presentmon_relative_ms\": ";
        if (e.relativeTimeValid)
            json << e.relativeTimeMs;
        else
            json << "null";
        json << ",\n";
        json << "      \"video_bookmark_ms\": ";
        if (e.videoBookmarkValid)
            json << e.videoBookmarkMs;
        else
            json << "null";
        json << ",\n";
        json << "      \"video_bookmark\": ";
        if (e.videoBookmarkValid)
            json << "\"" << formatVideoBookmark(e.videoBookmarkMs) << "\"";
        else
            json << "null";
        json << ",\n";
        json << "      \"frame_time_ms\": " << e.frameTimeMs << ",\n";
        json << "      \"cpu_busy_ms\": " << e.cpuBusyMs << ",\n";
        json << "      \"cpu_wait_ms\": " << e.cpuWaitMs << ",\n";
        json << "      \"gpu_time_ms\": " << e.gpuTimeMs << ",\n";
        json << "      \"gpu_busy_ms\": " << e.gpuBusyMs << ",\n";
        json << "      \"gpu_wait_ms\": " << e.gpuWaitMs << ",\n";
        json << "      \"present_mode\": \"" << jsonEscape(e.presentMode) << "\",\n";
        json << "      \"previous_present_mode\": \"" << jsonEscape(e.previousPresentMode) << "\",\n";
        json << "      \"present_mode_changed\": "
             << (e.presentModeChanged ? "true" : "false") << ",\n";
        json << "      \"telemetry_matched\": "
             << (e.telemetryMatched ? "true" : "false");

        if (e.telemetryMatched) {
            json << ",\n";
            json << "      \"telemetry_delta_ms\": " << e.telemetryDeltaMs << ",\n";
            json << "      \"process_cpu_pct\": " << e.processCpuPct << ",\n";
            json << "      \"system_cpu_pct\": " << e.systemCpuPct << ",\n";
            json << "      \"working_set_mib\": " << e.workingSetMiB << ",\n";
            json << "      \"private_usage_mib\": " << e.privateUsageMiB << ",\n";
            json << "      \"process_read_mib_s\": " << e.processReadMiBps << ",\n";
            json << "      \"process_write_mib_s\": " << e.processWriteMiBps << ",\n";
            json << "      \"system_memory_load_pct\": " << e.systemMemoryLoadPct << ",\n";
            json << "      \"system_memory_available_mib\": " << e.systemMemoryAvailableMiB << ",\n";
            json << "      \"gpu_util_pct\": " << e.gpuUtilPct << ",\n";
            json << "      \"gpu_mem_util_pct\": " << e.gpuMemoryUtilPct << ",\n";
            json << "      \"vram_used_mib\": " << e.vramUsedMiB << ",\n";
            json << "      \"vram_total_mib\": " << e.vramTotalMiB << ",\n";
            json << "      \"gpu_temp_c\": " << e.gpuTempC << ",\n";
            json << "      \"gpu_graphics_mhz\": " << e.gpuGraphicsMHz << ",\n";
            json << "      \"gpu_memory_mhz\": " << e.gpuMemoryMHz << ",\n";
            json << "      \"gpu_power_w\": " << e.gpuPowerW << ",\n";
            json << "      \"gpu_power_limit_w\": " << e.gpuPowerLimitW << ",\n";
        } else {
            json << ",\n";
        }

        json << "      \"telemetry_window\": {\"before\":";
        writeTelemetryPointJson(json, e.before);
        json << ",\"nearest\":";
        writeTelemetryPointJson(json, e.nearest);
        json << ",\"after\":";
        writeTelemetryPointJson(json, e.after);
        json << "},\n";

        json << "      \"telemetry_deltas\": {\"before_to_nearest\":";
        writeTelemetryDeltaJson(json, e.beforeToNearest);
        json << ",\"nearest_to_after\":";
        writeTelemetryDeltaJson(json, e.nearestToAfter);
        json << ",\"before_to_after\":";
        writeTelemetryDeltaJson(json, e.beforeToAfter);
        json << "},\n";

        json << "      \"signals\": [";
        for (std::size_t signalIndex = 0;
             signalIndex < e.signals.size();
             ++signalIndex) {
            if (signalIndex)
                json << ", ";
            json << "\"" << jsonEscape(e.signals[signalIndex]) << "\"";
        }
        json << "],\n";

        json << "      \"diagnostic_indicators\": [";
        for (std::size_t indicatorIndex = 0;
             indicatorIndex < e.diagnosticIndicators.size();
             ++indicatorIndex) {
            if (indicatorIndex)
                json << ", ";
            json << "\"" << jsonEscape(e.diagnosticIndicators[indicatorIndex]) << "\"";
        }
        json << "]\n";

        json << "    }" << (i + 1 < result.events.size() ? "," : "") << "\n";
    }

    json << "  ]\n";
    json << "}\n";

    if (!json) {
        if (error)
            *error = L"Failed while writing stutter_events.json";
        return false;
    }

    std::ofstream text(textOutput, std::ios::binary | std::ios::trunc);
    if (!text) {
        if (error)
            *error = L"Cannot create stutter_events.txt";
        return false;
    }

    text << std::fixed << std::setprecision(2);
    text << "Bodycam Diagnostics stutter correlation\n";
    text << "Threshold: 100.00 ms\n";
    text << "Telemetry window: before=start previous, nearest=start nearest, after=end next\n";
    text << "Video bookmarks: "
         << (result.videoBookmarkAlignmentAvailable ? "estimated" : "unavailable")
         << " (navigation aid only; not frame-accurate)\n";
    text << "Events: " << result.events.size() << "\n";

    for (std::size_t i = 0; i < result.events.size(); ++i) {
        const auto& e = result.events[i];

        text << "\nEvent " << (i + 1) << "\n";
        text << "Time: " << e.timestamp << "\n";
        if (e.relativeTimeValid)
            text << "PresentMon relative: " << e.relativeTimeMs << " ms\n";
        if (e.videoBookmarkValid)
            text << "Video bookmark estimate: "
                 << formatVideoBookmark(e.videoBookmarkMs)
                 << " (" << e.videoBookmarkMs << " ms)\n";
        else
            text << "Video bookmark estimate: unavailable\n";
        text << "Frametime: " << e.frameTimeMs << " ms\n";
        text << "CPU busy / wait: "
             << e.cpuBusyMs << " / " << e.cpuWaitMs << " ms\n";
        text << "GPU time / busy / wait: "
             << e.gpuTimeMs << " / "
             << e.gpuBusyMs << " / "
             << e.gpuWaitMs << " ms\n";
        text << "Present mode: " << e.presentMode;
        if (!e.previousPresentMode.empty())
            text << " (previous: " << e.previousPresentMode
                 << ", changed=" << (e.presentModeChanged ? "yes" : "no") << ")";
        text << "\n";

        if (e.telemetryMatched) {
            text << "Telemetry delta: " << e.telemetryDeltaMs << " ms\n";
            text << "Bodycam CPU / system CPU: "
                 << e.processCpuPct << " / " << e.systemCpuPct << " %\n";
            text << "Bodycam working/private memory: "
                 << e.workingSetMiB << " / "
                 << e.privateUsageMiB << " MiB\n";
            text << "Bodycam read/write: "
                 << e.processReadMiBps << " / "
                 << e.processWriteMiBps << " MiB/s\n";
            text << "System memory load / available: "
                 << e.systemMemoryLoadPct << " % / "
                 << e.systemMemoryAvailableMiB << " MiB\n";
            text << "GPU util / memory util: "
                 << e.gpuUtilPct << " / "
                 << e.gpuMemoryUtilPct << " %\n";
            text << "VRAM used / total: "
                 << e.vramUsedMiB << " / "
                 << e.vramTotalMiB << " MiB\n";
            text << "GPU temp: " << e.gpuTempC << " C\n";
            text << "GPU graphics / memory clock: "
                 << e.gpuGraphicsMHz << " / "
                 << e.gpuMemoryMHz << " MHz\n";
            text << "GPU power / limit: "
                 << e.gpuPowerW << " / "
                 << e.gpuPowerLimitW << " W\n";
        } else {
            text << "Telemetry: no sample within 1500 ms\n";
        }

        text << "Telemetry window [before / nearest / after]: ";
        writePointText(text, e.before);
        text << " / ";
        writePointText(text, e.nearest);
        text << " / ";
        writePointText(text, e.after);
        text << "\n";

        text << "Telemetry deltas [before->nearest / nearest->after]: ";
        if (e.beforeToNearest.valid) {
            text << "GPU " << e.beforeToNearest.gpuUtilPct << " %, "
                 << "clock " << e.beforeToNearest.gpuGraphicsMHz << " MHz, "
                 << "power " << e.beforeToNearest.gpuPowerW << " W, "
                 << "read " << e.beforeToNearest.processReadMiBps << " MiB/s";
        } else {
            text << "none";
        }

        text << " / ";

        if (e.nearestToAfter.valid) {
            text << "GPU " << e.nearestToAfter.gpuUtilPct << " %, "
                 << "clock " << e.nearestToAfter.gpuGraphicsMHz << " MHz, "
                 << "power " << e.nearestToAfter.gpuPowerW << " W, "
                 << "read " << e.nearestToAfter.processReadMiBps << " MiB/s";
        } else {
            text << "none";
        }
        text << "\n";

        text << "Signals: ";
        if (e.signals.empty()) {
            text << "none\n";
        } else {
            for (std::size_t signalIndex = 0;
                 signalIndex < e.signals.size();
                 ++signalIndex) {
                if (signalIndex)
                    text << ", ";
                text << e.signals[signalIndex];
            }
            text << "\n";
        }

        text << "Diagnostic indicators: ";
        if (e.diagnosticIndicators.empty()) {
            text << "none\n";
        } else {
            for (std::size_t indicatorIndex = 0;
                 indicatorIndex < e.diagnosticIndicators.size();
                 ++indicatorIndex) {
                if (indicatorIndex)
                    text << ", ";
                text << e.diagnosticIndicators[indicatorIndex];
            }
            text << "\n";
        }
    }

    if (!text) {
        if (error)
            *error = L"Failed while writing stutter_events.txt";
        return false;
    }

    return true;
}

