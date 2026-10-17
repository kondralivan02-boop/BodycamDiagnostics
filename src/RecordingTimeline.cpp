#include "RecordingTimeline.h"

void RecordingTimeline::start(std::chrono::steady_clock::time_point at) {
    if (state_ == RecordingState::Recording) return;
    intervals_.push_back({at, at});
    state_ = RecordingState::Recording;
}

void RecordingTimeline::close(std::chrono::steady_clock::time_point at) {
    if (state_ == RecordingState::Recording && !intervals_.empty())
        intervals_.back().end = at > intervals_.back().start ? at : intervals_.back().start;
}

void RecordingTimeline::stop(std::chrono::steady_clock::time_point at) {
    close(at);
    state_ = RecordingState::Stopped;
}

void RecordingTimeline::lost(std::chrono::steady_clock::time_point at) {
    close(at);
    state_ = RecordingState::Lost;
    hadFailure_ = true;
}

double RecordingTimeline::requestedSeconds() const noexcept {
    double result = 0;
    for (const auto& interval : intervals_)
        result += std::chrono::duration<double>(interval.end - interval.start).count();
    return result;
}
