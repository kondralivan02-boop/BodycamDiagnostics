#include "FrameContinuity.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace {
bool timestampMs(const std::string& text, std::int64_t& result) {
    int y = 0, m = 0, d = 0, h = 0, min = 0, sec = 0;
#ifdef _MSC_VER
    if (sscanf_s(text.c_str(), "%d-%d-%d %d:%d:%d", &y, &m, &d, &h, &min, &sec) != 6) return false;
#else
    if (std::sscanf(text.c_str(), "%d-%d-%d %d:%d:%d", &y, &m, &d, &h, &min, &sec) != 6) return false;
#endif
    const std::chrono::year_month_day date{std::chrono::year{y}, std::chrono::month{static_cast<unsigned>(m)}, std::chrono::day{static_cast<unsigned>(d)}};
    if (!date.ok() || h > 23 || min > 59 || sec > 60) return false;
    int milliseconds = 0;
    const auto dot = text.find('.');
    if (dot != std::string::npos) {
        int factor = 100;
        for (std::size_t i = dot + 1; i < text.size() && factor; ++i, factor /= 10) {
            if (text[i] < '0' || text[i] > '9') break;
            milliseconds += (text[i] - '0') * factor;
        }
    }
    result = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::sys_days{date}.time_since_epoch()).count()
        + ((h * 60 + min) * 60 + sec) * 1000LL + milliseconds;
    return true;
}
}

bool FrameContinuity::accept(const std::string& timestamp, double frameTimeMs, const std::string& presentMode) {
    std::int64_t now = 0;
    const bool hasTime = timestampMs(timestamp, now);
    bool gap = false;
    if (hasTime && hasPrevious_) {
        const double elapsed = static_cast<double>(now - previousMs_);
        gap = elapsed < 0 || elapsed > 500.0 ||
            (frameTimeMs > 500.0 && !presentMode.empty() && !previousMode_.empty() && presentMode != previousMode_);
    }
    if (gap) intervals_.push_back({previousTimestamp_, timestamp});
    if (hasTime) { previousMs_ = now; hasPrevious_ = true; previousTimestamp_ = timestamp; }
    if (!presentMode.empty()) previousMode_ = presentMode;
    if (gap) ++gaps_;
    return !gap && std::isfinite(frameTimeMs) && frameTimeMs > 0.0;
}
