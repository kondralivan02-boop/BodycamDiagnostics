#include "RecordingTimeline.h"
#include <chrono>
#include <stdexcept>

void check(bool condition) { if (!condition) throw std::runtime_error("recording timeline mismatch"); }

int main() {
    using clock = std::chrono::steady_clock;
    const auto base = clock::time_point{};
    RecordingTimeline timeline;
    check(timeline.state() == RecordingState::Disabled);
    timeline.start(base + std::chrono::seconds(2));
    check(timeline.state() == RecordingState::Recording);
    timeline.stop(base + std::chrono::seconds(5));
    timeline.start(base + std::chrono::seconds(10));
    timeline.lost(base + std::chrono::seconds(12));
    check(timeline.state() == RecordingState::Lost);
    check(timeline.intervals().size() == 2);
    check(timeline.requestedSeconds() == 5.0);
}
