#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct LiveFrameSnapshot {
    bool valid = false;
    std::size_t totalFrames = 0;
    double fps = 0.0;
    double frameTimeMs = 0.0;
    double onePercentLowFps = 0.0;
    double pointOnePercentLowFps = 0.0;
};

class LiveFrameMetrics {
public:
    void reset(const std::filesystem::path& csvPath);
    bool poll();
    LiveFrameSnapshot snapshot() const;

private:
    bool processLine(const std::string& line);
    bool processHeader(const std::string& line);

    std::filesystem::path csvPath_;
    std::uintmax_t offset_ = 0;
    enum class TextEncoding {
        Unknown,
        Utf8,
        Utf16Le
    };

    std::string rawPending_;
    std::string pending_;
    TextEncoding encoding_ = TextEncoding::Unknown;
    bool headerReady_ = false;
    std::size_t frameTimeIndex_ = static_cast<std::size_t>(-1);
    std::vector<double> frameTimesMs_;
};
