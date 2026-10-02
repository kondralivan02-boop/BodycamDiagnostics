#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

class FrameContinuity {
public:
    bool accept(const std::string& timestamp, double frameTimeMs, const std::string& presentMode);
    std::size_t gapCount() const noexcept { return gaps_; }
private:
    std::int64_t previousMs_ = 0;
    bool hasPrevious_ = false;
    std::string previousMode_;
    std::size_t gaps_ = 0;
};
