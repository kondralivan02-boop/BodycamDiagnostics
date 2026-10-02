#include "MetricsCalculator.h"
#include "AtomicOutputFile.h"
#include "FrameContinuity.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
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
        } else {
            field.push_back(c);
        }
    }
    fields.push_back(std::move(field));
    return fields;
}

std::string trimBom(std::string s) {
    if (s.size() >= 3 &&
        static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        s.erase(0, 3);
    }
    return s;
}

bool tryParsePositiveDouble(const std::string& text, double& value) {
    if (text.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || *end != '\0' || !std::isfinite(parsed) || parsed <= 0.0)
        return false;
    value = parsed;
    return true;
}

double slowestTailFps(const std::vector<double>& sortedFrameTimes, double fraction) {
    if (sortedFrameTimes.empty()) return 0.0;

    const auto count = (std::max)(
        std::size_t{1},
        static_cast<std::size_t>(std::ceil(sortedFrameTimes.size() * fraction)));

    const auto actual = (std::min)(count, sortedFrameTimes.size());
    const auto begin = sortedFrameTimes.end() - static_cast<std::ptrdiff_t>(actual);
    const double totalMs = std::accumulate(begin, sortedFrameTimes.end(), 0.0);
    const double meanMs = totalMs / static_cast<double>(actual);

    return meanMs > 0.0 ? 1000.0 / meanMs : 0.0;
}

double percentileSorted(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    if (sorted.size() == 1) return sorted.front();

    const double position = std::clamp(p, 0.0, 1.0) * static_cast<double>(sorted.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(position));
    const auto hi = static_cast<std::size_t>(std::ceil(position));
    if (lo == hi) return sorted[lo];
    const double fraction = position - static_cast<double>(lo);
    return sorted[lo] + (sorted[hi] - sorted[lo]) * fraction;
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

std::wstring widen(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

std::string utf16LeBytesToUtf8(const std::string& bytes) {
    if (bytes.empty())
        return {};

    const std::size_t charCount = bytes.size() / 2;
    std::wstring wide;
    wide.resize(charCount);

    for (std::size_t i = 0; i < charCount; ++i) {
        const auto lo = static_cast<unsigned char>(bytes[i * 2]);
        const auto hi = static_cast<unsigned char>(bytes[i * 2 + 1]);
        wide[i] = static_cast<wchar_t>(
            static_cast<unsigned>(lo) |
            (static_cast<unsigned>(hi) << 8));
    }

    const int required = WideCharToMultiByte(
        CP_UTF8, 0,
        wide.data(), static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr);

    if (required <= 0)
        return {};

    std::string out(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0,
        wide.data(), static_cast<int>(wide.size()),
        out.data(), required, nullptr, nullptr);

    return out;
}

bool loadCsvTextUtf8(const std::filesystem::path& path,
                     std::string& text) {
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
}

MetricsSummary calculateMetrics(const std::filesystem::path& presentMonCsv) {
    MetricsSummary result;

    std::string csvText;
    if (!loadCsvTextUtf8(presentMonCsv, csvText)) {
        result.error = L"Cannot open presentmon.csv";
        return result;
    }

    std::istringstream in(csvText);

    std::string headerLine;
    if (!std::getline(in, headerLine)) {
        result.error = L"presentmon.csv is empty";
        return result;
    }

    auto headers = parseCsvLine(trimBom(headerLine));
    if (headers.empty()) {
        result.error = L"presentmon.csv has no header";
        return result;
    }

    const std::vector<std::string> candidates = {
        "FrameTime", "CPUFrameTime", "DisplayedTime"
    };

    std::size_t frameTimeIndex = std::numeric_limits<std::size_t>::max();
    for (const auto& candidate : candidates) {
        const auto it = std::find(headers.begin(), headers.end(), candidate);
        if (it != headers.end()) {
            frameTimeIndex = static_cast<std::size_t>(std::distance(headers.begin(), it));
            result.frameTimeColumn = candidate;
            break;
        }
    }

    if (frameTimeIndex == std::numeric_limits<std::size_t>::max()) {
        std::ostringstream names;
        for (std::size_t i = 0; i < headers.size(); ++i) {
            if (i) names << ", ";
            names << headers[i];
        }
        result.error = L"No supported frame-time column. CSV columns: " + widen(names.str());
        return result;
    }

    std::vector<double> frameTimes;
    frameTimes.reserve(120000);
    const auto timestampIt = std::find(headers.begin(), headers.end(), "CPUStartDateTime");
    const auto modeIt = std::find(headers.begin(), headers.end(), "PresentMode");
    const auto timeIndex = static_cast<std::size_t>(std::distance(headers.begin(), timestampIt));
    const auto modeIndex = static_cast<std::size_t>(std::distance(headers.begin(), modeIt));
    FrameContinuity continuity;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const auto fields = parseCsvLine(line);
        if (frameTimeIndex >= fields.size()) continue;

        double frameTimeMs = 0.0;
        if (!tryParsePositiveDouble(fields[frameTimeIndex], frameTimeMs)) continue;

        if (timeIndex < fields.size() && !continuity.accept(fields[timeIndex], frameTimeMs,
                modeIndex < fields.size() ? fields[modeIndex] : "")) continue;
        if (frameTimeMs > 10000.0) continue;
        frameTimes.push_back(frameTimeMs);
    }
    result.captureGaps = continuity.gapCount();

    if (frameTimes.size() < 2) {
        result.error = L"Not enough valid frame-time samples in presentmon.csv";
        return result;
    }

    const double totalMs = std::accumulate(frameTimes.begin(), frameTimes.end(), 0.0);
    result.totalFrames = frameTimes.size();
    result.durationSeconds = totalMs / 1000.0;
    result.averageFrameTimeMs = totalMs / static_cast<double>(frameTimes.size());
    result.averageFps = result.durationSeconds > 0.0
        ? static_cast<double>(result.totalFrames) / result.durationSeconds
        : 0.0;

    result.stuttersOver50Ms = static_cast<std::size_t>(std::count_if(
        frameTimes.begin(), frameTimes.end(), [](double ms) { return ms >= 50.0; }));
    result.stuttersOver100Ms = static_cast<std::size_t>(std::count_if(
        frameTimes.begin(), frameTimes.end(), [](double ms) { return ms >= 100.0; }));
    result.stuttersOver250Ms = static_cast<std::size_t>(std::count_if(
        frameTimes.begin(), frameTimes.end(), [](double ms) { return ms >= 250.0; }));
    result.maxFrameTimeMs = *std::max_element(frameTimes.begin(), frameTimes.end());

    std::sort(frameTimes.begin(), frameTimes.end());
    result.p50FrameTimeMs = percentileSorted(frameTimes, 0.50);
    result.p95FrameTimeMs = percentileSorted(frameTimes, 0.95);
    result.p99FrameTimeMs = percentileSorted(frameTimes, 0.99);
    result.p999FrameTimeMs = percentileSorted(frameTimes, 0.999);
    result.onePercentLowFps = slowestTailFps(frameTimes, 0.01);
    result.pointOnePercentLowFps = slowestTailFps(frameTimes, 0.001);
    result.valid = true;
    return result;
}

bool writeMetricsReport(const MetricsSummary& s,
                        const std::filesystem::path& jsonOutput,
                        const std::filesystem::path& textOutput,
                        std::wstring* error) {
    if (!s.valid) {
        if (error) *error = s.error.empty() ? L"Metrics summary is invalid" : s.error;
        return false;
    }

    AtomicOutputFile jsonFile(jsonOutput);
    auto& json = jsonFile.stream();
    if (!jsonFile.isOpen()) {
        if (error) *error = L"Cannot create summary.json";
        return false;
    }

    json << std::fixed << std::setprecision(3)
         << "{\n"
         << "  \"frame_time_column\": \"" << jsonEscape(s.frameTimeColumn) << "\",\n"
         << "  \"total_frames\": " << s.totalFrames << ",\n"
         << "  \"capture_gaps\": " << s.captureGaps << ",\n"
         << "  \"duration_seconds\": " << s.durationSeconds << ",\n"
         << "  \"average_fps\": " << s.averageFps << ",\n"
         << "  \"average_frame_time_ms\": " << s.averageFrameTimeMs << ",\n"
         << "  \"low_method\": \"inverse_mean_slowest_frametimes\",\n"
         << "  \"one_percent_low_fps\": " << s.onePercentLowFps << ",\n"
         << "  \"point_one_percent_low_fps\": " << s.pointOnePercentLowFps << ",\n"
         << "  \"p50_frame_time_ms\": " << s.p50FrameTimeMs << ",\n"
         << "  \"p95_frame_time_ms\": " << s.p95FrameTimeMs << ",\n"
         << "  \"p99_frame_time_ms\": " << s.p99FrameTimeMs << ",\n"
         << "  \"p999_frame_time_ms\": " << s.p999FrameTimeMs << ",\n"
         << "  \"stutters_over_50ms\": " << s.stuttersOver50Ms << ",\n"
         << "  \"stutters_over_100ms\": " << s.stuttersOver100Ms << ",\n"
         << "  \"stutters_over_250ms\": " << s.stuttersOver250Ms << ",\n"
         << "  \"max_frame_time_ms\": " << s.maxFrameTimeMs << "\n"
         << "}\n";
    AtomicOutputFile textFile(textOutput);
    auto& text = textFile.stream();
    if (!textFile.isOpen()) {
        if (error) *error = L"Cannot create summary.txt";
        return false;
    }

    text << std::fixed << std::setprecision(2)
         << "Bodycam Diagnostics summary\n"
         << "Frame-time source: " << s.frameTimeColumn << "\n"
         << "Frames: " << s.totalFrames << "\n"
         << "Duration: " << s.durationSeconds << " s\n"
         << "Average FPS: " << s.averageFps << "\n"
         << "Low method: inverse mean of slowest frametimes\n"
         << "1% low: " << s.onePercentLowFps << " FPS\n"
         << "0.1% low: " << s.pointOnePercentLowFps << " FPS\n"
         << "Average frametime: " << s.averageFrameTimeMs << " ms\n"
         << "Capture gaps: " << s.captureGaps << "\n"
         << "P50 / P95 / P99 / P99.9: "
         << s.p50FrameTimeMs << " / " << s.p95FrameTimeMs << " / "
         << s.p99FrameTimeMs << " / " << s.p999FrameTimeMs << " ms\n"
         << "Stutters >50 ms: " << s.stuttersOver50Ms << "\n"
         << "Stutters >100 ms: " << s.stuttersOver100Ms << "\n"
         << "Stutters >250 ms: " << s.stuttersOver250Ms << "\n"
         << "Maximum frametime: " << s.maxFrameTimeMs << " ms\n";

    if (!json) {
        if (error)
            *error = L"Failed while writing summary.json";
        return false;
    }

    if (!text) {
        if (error)
            *error = L"Failed while writing summary.txt";
        return false;
    }

    if (!jsonFile.commit(error))
        return false;
    if (!textFile.commit(error))
        return false;

    return true;
}
