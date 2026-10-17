#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct CaptureGap { std::string start; std::string end; };

class FrameContinuity {
public:
    bool accept(const std::string& timestamp, double frameTimeMs, const std::string& presentMode);
    std::size_t gapCount() const noexcept { return gaps_; }
    const std::vector<CaptureGap>& gaps() const noexcept { return intervals_; }
private:
    std::int64_t previousMs_ = 0;
    bool hasPrevious_ = false;
    std::string previousMode_;
    std::string previousTimestamp_;
    std::vector<CaptureGap> intervals_;
    std::size_t gaps_ = 0;
};
