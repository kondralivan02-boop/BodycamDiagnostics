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
    check(requiredCaptureSourcesReady(true, true),
          "both required sources should permit startup");

    check(!requiredCaptureSourcesReady(false, true),
          "missing PresentMon must block startup");

    check(!requiredCaptureSourcesReady(true, false),
          "missing recorder must block startup");

    check(!requiredCaptureSourcesReady(false, false),
          "missing both required sources must block startup");

    std::cout << "Capture startup policy tests passed\n";
}
