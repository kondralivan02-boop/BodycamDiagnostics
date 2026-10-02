#include "EnvironmentDiagnostics.h"
#include "AtomicOutputFile.h"
#include <Windows.h>
#include <winevt.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>

namespace {
constexpr std::uint64_t kEpoch100ns = 116444736000000000ULL;
constexpr DWORD kMaxBytes = 64 * 1024;
struct EventHandle {
    EVT_HANDLE value = nullptr;
    explicit EventHandle(EVT_HANDLE handle) : value(handle) {}
    ~EventHandle() { if (value) EvtClose(value); }
    EventHandle(const EventHandle&) = delete;
    EventHandle& operator=(const EventHandle&) = delete;
};
std::string utf8(const wchar_t* value) {
    if (!value || !*value) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}
std::string errorStatus(DWORD error) {
    if (error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD) return "access_denied";
    if (error == ERROR_EVT_CHANNEL_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_FILE_NOT_FOUND) return "not_found";
    if (error == ERROR_TIMEOUT) return "timeout";
    return "error";
}
std::int64_t unixMs(std::chrono::system_clock::time_point time) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}
std::wstring utcQueryTime(std::chrono::system_clock::time_point time) {
    ULARGE_INTEGER u{};
    u.QuadPart = static_cast<std::uint64_t>(unixMs(time)) * 10000ULL + kEpoch100ns;
    FILETIME ft{u.LowPart, u.HighPart};
    SYSTEMTIME st{};
    FileTimeToSystemTime(&ft, &st);
    std::wostringstream out;
    out.imbue(std::locale::classic());
    out << std::setfill(L'0') << std::setw(4) << st.wYear << L'-' << std::setw(2) << st.wMonth
        << L'-' << std::setw(2) << st.wDay << L'T' << std::setw(2) << st.wHour
        << L':' << std::setw(2) << st.wMinute << L':' << std::setw(2) << st.wSecond
        << L'.' << std::setw(3) << st.wMilliseconds << L'Z';
    return out.str();
}
bool renderEvent(EVT_HANDLE context, EVT_HANDLE event, WindowsEventEvidence& output, DWORD& error) {
    DWORD bytes = 0, count = 0;
    EvtRender(context, event, EvtRenderEventValues, 0, nullptr, &bytes, &count);
    error = GetLastError();
    if (error != ERROR_INSUFFICIENT_BUFFER) return false;
    if (bytes > kMaxBytes) { error = ERROR_MORE_DATA; return false; }
    std::vector<EVT_VARIANT> buffer((bytes + sizeof(EVT_VARIANT) - 1) / sizeof(EVT_VARIANT));
    if (!EvtRender(context, event, EvtRenderEventValues, bytes, buffer.data(), &bytes, &count)) {
        error = GetLastError(); return false;
    }
    if (count < EvtSystemPropertyIdEND || buffer[EvtSystemTimeCreated].Type != EvtVarTypeFileTime ||
        buffer[EvtSystemEventID].Type != EvtVarTypeUInt16 || buffer[EvtSystemEventRecordId].Type != EvtVarTypeUInt64) {
        error = ERROR_INVALID_DATA; return false;
    }
    const auto time = buffer[EvtSystemTimeCreated].FileTimeVal;
    if (time < kEpoch100ns) { error = ERROR_INVALID_DATA; return false; }
    output.utcUnixMs = static_cast<std::int64_t>((time - kEpoch100ns) / 10000ULL);
    output.eventId = buffer[EvtSystemEventID].UInt16Val;
    output.recordId = buffer[EvtSystemEventRecordId].UInt64Val;
    if (buffer[EvtSystemProviderName].Type == EvtVarTypeString)
        output.provider = utf8(buffer[EvtSystemProviderName].StringVal);
    bytes = 0;
    EvtRender(nullptr, event, EvtRenderEventXml, 0, nullptr, &bytes, &count);
    error = GetLastError();
    if (error != ERROR_INSUFFICIENT_BUFFER) return false;
    if (bytes > kMaxBytes) { error = ERROR_MORE_DATA; return false; }
    std::vector<wchar_t> xml((bytes + sizeof(wchar_t) - 1) / sizeof(wchar_t));
    if (!EvtRender(nullptr, event, EvtRenderEventXml, bytes, xml.data(), &bytes, &count)) {
        error = GetLastError(); return false;
    }
    output.xml = utf8(xml.data());
    return true;
}
SourceCapability readChannel(const wchar_t* channel, const std::wstring& query,
                             std::vector<WindowsEventEvidence>* output) {
    SourceCapability status{std::string("event_log_") + utf8(channel), "not_probed", 0, true, "current token; read only"};
    EventHandle handle(EvtQuery(nullptr, channel, query.c_str(), EvtQueryChannelPath | EvtQueryReverseDirection));
    if (!handle.value) {
        status.errorCode = GetLastError(); status.status = errorStatus(status.errorCode); return status;
    }
    EventHandle context(output ? EvtCreateRenderContext(0, nullptr, EvtRenderContextSystem) : nullptr);
    if (output && !context.value) {
        status.errorCode = GetLastError(); status.status = errorStatus(status.errorCode); return status;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    std::size_t seen = 0, saved = 0;
    const std::size_t limit = output ? 256 : 1;
    status.status = "available_empty";
    while (seen < limit) {
        if (std::chrono::steady_clock::now() >= deadline) {
            status.status = "partial"; status.errorCode = ERROR_TIMEOUT; break;
        }
        EVT_HANDLE raw = nullptr;
        DWORD returned = 0;
        if (!EvtNext(handle.value, 1, &raw, 50, 0, &returned)) {
            const DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_ITEMS) {
                status.errorCode = error;
                status.status = seen ? "partial" : errorStatus(error);
            }
            break;
        }
        EventHandle event(raw);
        ++seen;
        if (!output) { status.status = "available"; break; }
        WindowsEventEvidence evidence;
        evidence.channel = utf8(channel);
        DWORD renderError = 0;
        if (renderEvent(context.value, event.value, evidence, renderError)) {
            output->push_back(std::move(evidence)); ++saved;
            if (!status.errorCode) status.status = "available";
        } else {
            status.errorCode = renderError; status.status = "partial";
        }
    }
    if (output && seen == limit) {
        status.status = "partial";
        status.detail += "; entry limit reached (may be truncated)";
    }
    if (output) status.detail += "; enumerated=" + std::to_string(seen) + "; saved=" + std::to_string(saved);
    return status;
}
}
EnvironmentCapabilities probeEnvironmentCapabilities(std::uint32_t gamePid) {
    EnvironmentCapabilities profile;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, gamePid);
    const DWORD processError = process ? ERROR_SUCCESS : GetLastError();
    if (process) CloseHandle(process);
    profile.sources.push_back({"game_process_query", process ? "available" : errorStatus(processError), processError, true,
                               "PROCESS_QUERY_LIMITED_INFORMATION only; does not prove all telemetry counters are readable"});
    for (const wchar_t* channel : {L"System", L"Application"})
        profile.sources.push_back(readChannel(channel, L"*", nullptr));
    return profile;
}
bool saveEnvironmentCapabilities(const EnvironmentCapabilities& profile, const std::filesystem::path& output) {
    AtomicOutputFile outputFile(output);
    auto& file = outputFile.stream();
    if (!outputFile.isOpen())
        return false;

    writeCapabilitiesJson(file, profile);
    if (!file)
        return false;

    return outputFile.commit();
}
EventLogCollection collectWindowsEvents(std::chrono::system_clock::time_point start,
                                       std::chrono::system_clock::time_point end) {
    EventLogCollection result;
    if (end < start) {
        result.channels.push_back({"event_log", "error", ERROR_INVALID_TIME, true, "wall clock reversed; query skipped"});
        return result;
    }
    const std::wstring timeFilter = L"TimeCreated[@SystemTime >= '" + utcQueryTime(start - std::chrono::seconds(2)) +
        L"' and @SystemTime <= '" + utcQueryTime(end + std::chrono::seconds(2)) + L"']";
    for (const wchar_t* channel : {L"System", L"Application"}) {
        // Include WER 1001 even when informational. Keep exact evidence; no causal classification.
        const std::wstring query = L"*[System[" + timeFilter + L" and ((Level >= 1 and Level <= 3) or EventID=1001)]]";
        result.channels.push_back(readChannel(channel, query, &result.events));
    }
    return result;
}
bool writeWindowsEventReport(const EventLogCollection& result, const StutterCorrelationResult& stutters,
                             std::chrono::system_clock::time_point presentMonStart,
                             std::chrono::system_clock::time_point sessionStart,
                             std::chrono::system_clock::time_point sessionEnd,
                             bool clockStable, bool gameExited, const std::filesystem::path& output) {
    AtomicOutputFile outputFile(output);
    auto& out = outputFile.stream();
    if (!outputFile.isOpen())
        return false;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(3);
    out << "{\n\"schema_version\":1,\n\"optional\":true,\n\"session_start_utc_unix_ms\":" << unixMs(sessionStart)
        << ",\n\"session_end_utc_unix_ms\":" << unixMs(sessionEnd)
        << ",\n\"query_padding_ms\":2000,\n\"clock_stable\":" << (clockStable ? "true" : "false")
        << ",\n\"alignment\":\"estimated_presentmon_launch_plus_first_frame_relative_time\",\n"
        << "\"tolerance_ms\":2000,\n\"interpretation\":\"Temporal association only, not proof of cause or Bodycam ownership. First-frame startup delay is unknown; 2000 ms is a search window, not an accuracy guarantee. Event log delivery may be delayed; missing events do not exclude a fault. No post-reboot recovery in this probe.\",\n"
        << "\"channels\":";
    writeCapabilitiesJson(out, EnvironmentCapabilities{result.channels});
    out << ",\n\"events\":[";
    bool first = true;
    for (const auto& event : result.events) {
        if (!first) out << ',';
        first = false;
        out << "\n{\"channel\":" << jsonQuoted(event.channel) << ",\"provider\":" << jsonQuoted(event.provider)
            << ",\"event_id\":" << event.eventId << ",\"record_id\":" << event.recordId
            << ",\"utc_unix_ms\":" << event.utcUnixMs
            << ",\"near_observed_game_exit\":" << (clockStable && gameExited && isTemporalMatch(static_cast<double>(event.utcUnixMs), static_cast<double>(unixMs(sessionEnd)), 0, 2000) ? "true" : "false")
            << ",\"stutter_matches\":[";
        bool firstMatch = true;
        if (clockStable) for (std::size_t i = 0; i < stutters.events.size(); ++i) {
            const auto& stall = stutters.events[i];
            if (!stall.relativeTimeValid) continue;
            const double startMs = static_cast<double>(unixMs(presentMonStart)) + stall.relativeTimeMs;
            if (!isTemporalMatch(static_cast<double>(event.utcUnixMs), startMs, stall.frameTimeMs, 2000)) continue;
            if (!firstMatch) out << ',';
            firstMatch = false;
            out << "{\"stutter_index\":" << i << ",\"distance_from_interval_ms\":"
                << temporalDistanceMs(static_cast<double>(event.utcUnixMs), startMs, stall.frameTimeMs) << '}';
        }
        out << "],\"xml\":" << jsonQuoted(event.xml) << '}';
    }
    out << "\n]}\n";
    if (!out)
        return false;
    return outputFile.commit();
}
