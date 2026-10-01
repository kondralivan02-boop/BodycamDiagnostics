#include "LiveFrameMetrics.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <string>
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
            continue;
        }

        if (c == '"') quoted = true;
        else if (c == ',') {
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

bool parsePositiveDouble(const std::string& text, double& value) {
    if (text.empty()) return false;

    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' ||
        !std::isfinite(parsed) || parsed <= 0.0 || parsed > 10000.0) {
        return false;
    }

    value = parsed;
    return true;
}

double averageSlowestFps(const std::vector<double>& values, double fraction) {
    if (values.empty()) return 0.0;

    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());

    const std::size_t count = (std::max)(
        std::size_t{1},
        static_cast<std::size_t>(std::ceil(sorted.size() * fraction)));

    const auto begin = sorted.end() - static_cast<std::ptrdiff_t>(
        (std::min)(count, sorted.size()));

    double fpsSum = 0.0;
    for (auto it = begin; it != sorted.end(); ++it)
        fpsSum += 1000.0 / *it;

    return fpsSum / static_cast<double>(std::distance(begin, sorted.end()));
}

double recentAverageFps(const std::vector<double>& values, std::size_t frameCount) {
    if (values.empty()) return 0.0;

    const std::size_t count = (std::min)(frameCount, values.size());
    const auto begin = values.end() - static_cast<std::ptrdiff_t>(count);

    const double totalMs = std::accumulate(begin, values.end(), 0.0);
    return totalMs > 0.0
        ? static_cast<double>(count) * 1000.0 / totalMs
        : 0.0;
}

double recentAverageFrameTime(const std::vector<double>& values, std::size_t frameCount) {
    if (values.empty()) return 0.0;

    const std::size_t count = (std::min)(frameCount, values.size());
    const auto begin = values.end() - static_cast<std::ptrdiff_t>(count);

    const double totalMs = std::accumulate(begin, values.end(), 0.0);
    return totalMs / static_cast<double>(count);
}
}

void LiveFrameMetrics::reset(const std::filesystem::path& csvPath) {
    csvPath_ = csvPath;
    offset_ = 0;
    pending_.clear();
    headerReady_ = false;
    frameTimeIndex_ = static_cast<std::size_t>(-1);
    frameTimesMs_.clear();
}

bool LiveFrameMetrics::processHeader(const std::string& line) {
    auto headers = parseCsvLine(trimBom(line));

    static constexpr const char* candidates[] = {
        "FrameTime",
        "CPUFrameTime",
        "DisplayedTime"
    };

    for (const auto* candidate : candidates) {
        const auto it = std::find(headers.begin(), headers.end(), candidate);
        if (it != headers.end()) {
            frameTimeIndex_ = static_cast<std::size_t>(
                std::distance(headers.begin(), it));
            headerReady_ = true;
            return true;
        }
    }

    return false;
}

bool LiveFrameMetrics::processLine(const std::string& rawLine) {
    std::string line = rawLine;
    if (!line.empty() && line.back() == '\r')
        line.pop_back();

    if (line.empty()) return true;

    if (!headerReady_)
        return processHeader(line);

    const auto fields = parseCsvLine(line);
    if (frameTimeIndex_ >= fields.size())
        return true;

    double frameTimeMs = 0.0;
    if (!parsePositiveDouble(fields[frameTimeIndex_], frameTimeMs))
        return true;

    frameTimesMs_.push_back(frameTimeMs);

    constexpr std::size_t kMaxStoredFrames = 500000;
    if (frameTimesMs_.size() > kMaxStoredFrames) {
        const std::size_t removeCount = frameTimesMs_.size() - kMaxStoredFrames;
        frameTimesMs_.erase(
            frameTimesMs_.begin(),
            frameTimesMs_.begin() + static_cast<std::ptrdiff_t>(removeCount));
    }

    return true;
}

bool LiveFrameMetrics::poll() {
    if (csvPath_.empty()) return false;

    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(csvPath_, ec);
    if (ec) return false;

    if (fileSize < offset_) {
        reset(csvPath_);
    }

    std::ifstream in(csvPath_, std::ios::binary);
    if (!in) return false;

    in.seekg(static_cast<std::streamoff>(offset_), std::ios::beg);
    std::string chunk(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());

    offset_ += chunk.size();
    if (chunk.empty()) return true;

    pending_ += chunk;

    std::size_t start = 0;
    while (true) {
        const auto newline = pending_.find('\n', start);
        if (newline == std::string::npos)
            break;

        processLine(pending_.substr(start, newline - start));
        start = newline + 1;
    }

    if (start > 0)
        pending_.erase(0, start);

    return true;
}

LiveFrameSnapshot LiveFrameMetrics::snapshot() const {
    LiveFrameSnapshot s;
    if (!headerReady_ || frameTimesMs_.empty())
        return s;

    s.valid = true;
    s.totalFrames = frameTimesMs_.size();
    s.fps = recentAverageFps(frameTimesMs_, 60);
    s.frameTimeMs = recentAverageFrameTime(frameTimesMs_, 12);
    s.onePercentLowFps = averageSlowestFps(frameTimesMs_, 0.01);
    s.pointOnePercentLowFps = averageSlowestFps(frameTimesMs_, 0.001);
    return s;
}
