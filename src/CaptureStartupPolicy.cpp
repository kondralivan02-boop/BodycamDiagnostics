#include "CaptureStartupPolicy.h"

bool requiredCaptureSourcesReady(
    bool presentMonStarted,
    bool recorderStarted) noexcept {

    return presentMonStarted && recorderStarted;
}
