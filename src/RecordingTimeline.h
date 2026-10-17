#pragma once
#include <chrono>
#include <vector>

enum class RecordingState { Disabled, Starting, Recording, Lost, Stopped };

struct RecordingInterval {
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point end;
};

class RecordingTimeline {
public:
    RecordingState state() const noexcept { return state_; }
    bool hadFailure() const noexcept { return hadFailure_; }
    void starting() noexcept { state_ = RecordingState::Starting; }
    void start(std::chrono::steady_clock::time_point at);
    void stop(std::chrono::steady_clock::time_point at);
    void lost(std::chrono::steady_clock::time_point at);
    const std::vector<RecordingInterval>& intervals() const noexcept { return intervals_; }
    double requestedSeconds() const noexcept;
private:
    void close(std::chrono::steady_clock::time_point at);
    RecordingState state_ = RecordingState::Disabled;
    bool hadFailure_ = false;
    std::vector<RecordingInterval> intervals_;
};
