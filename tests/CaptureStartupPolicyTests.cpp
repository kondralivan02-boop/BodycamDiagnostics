#include "CaptureStartupPolicy.h"

#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
}

int main() {
    check(requiredCaptureSourcesReady(true, true, false, false),
          "no mirror configured should permit startup when capture sources are ready");

    check(requiredCaptureSourcesReady(true, true, true, true),
          "configured mirror should permit startup when it started");

    check(!requiredCaptureSourcesReady(true, true, true, false),
          "configured mirror must block startup when it failed");

    check(!requiredCaptureSourcesReady(false, true, false, false),
          "missing PresentMon must block startup");

    check(!requiredCaptureSourcesReady(true, false, false, false),
          "missing recorder must block startup");

    check(!requiredCaptureSourcesReady(false, false, false, false),
          "missing both required sources must block startup");

    std::cout << "Capture startup policy tests passed\n";
}
