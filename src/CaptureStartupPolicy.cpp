#include "CaptureStartupPolicy.h"

bool requiredCaptureSourcesReady(
    bool presentMonStarted,
    bool recorderStarted,
    bool mirrorConfigured,
    bool mirrorStarted) noexcept {

    return presentMonStarted &&
           recorderStarted &&
           (!mirrorConfigured || mirrorStarted);
}
