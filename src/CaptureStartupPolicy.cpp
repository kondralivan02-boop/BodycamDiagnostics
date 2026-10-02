#include "CaptureStartupPolicy.h"

bool requiredCaptureSourcesReady(
    bool presentMonStarted,
    bool recorderStarted) noexcept {

    (void)presentMonStarted;
    (void)recorderStarted;

    // RED baseline: current UI effectively allows the session to continue
    // after an individual required source reports startup failure.
    return true;
}
