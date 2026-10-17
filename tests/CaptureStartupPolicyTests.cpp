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
    check(requiredCaptureSourcesReady(true, false, false),
          "diagnostics should start without video when PresentMon is available");

    check(requiredCaptureSourcesReady(true, true, true),
          "configured mirror should permit startup when it started");

    check(!requiredCaptureSourcesReady(true, true, false),
          "configured mirror must block startup when it failed");

    check(!requiredCaptureSourcesReady(false, false, false),
          "missing PresentMon must block startup");


    std::cout << "Capture startup policy tests passed\n";
}
