#include "CaptureStartupPolicy.h"

bool requiredCaptureSourcesReady(
    bool presentMonStarted,
    bool mirrorConfigured,
    bool mirrorStarted) noexcept {

    return presentMonStarted &&
           (!mirrorConfigured || mirrorStarted);
}
