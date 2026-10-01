#include "StutterCorrelator.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

void classifyStutterIndicators(StutterEvent& event);

namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}

bool hasSignal(const StutterEvent& event, const char* signal) {
    return std::find(event.signals.begin(), event.signals.end(), signal) !=
           event.signals.end();
}

bool hasIndicator(const StutterEvent& event, const char* indicator) {
    return std::find(event.diagnosticIndicators.begin(), event.diagnosticIndicators.end(), indicator) !=
           event.diagnosticIndicators.end();
}
}

int main() {
    StutterEvent cpu{};
    cpu.frameTimeMs = 250.0;
    cpu.cpuBusyMs = 180.0;
    classifyStutterIndicators(cpu);
    check(hasIndicator(cpu, "CPU_STALL_INDICATOR"),
          "CPU-dominant stall indicator");

    StutterEvent gpuBusy{};
    gpuBusy.frameTimeMs = 250.0;
    gpuBusy.gpuBusyMs = 180.0;
    classifyStutterIndicators(gpuBusy);
    check(hasIndicator(gpuBusy, "GPU_BUSY_STALL_INDICATOR"),
          "GPU-busy stall indicator");

    StutterEvent gpuIdle{};
    gpuIdle.frameTimeMs = 300.0;
    gpuIdle.telemetryMatched = true;
    gpuIdle.gpuUtilPct = 8.0;
    classifyStutterIndicators(gpuIdle);
    check(hasIndicator(gpuIdle, "GPU_IDLE_STALL_INDICATOR"),
          "GPU-idle stall indicator");

    StutterEvent io{};
    io.frameTimeMs = 150.0;
    io.beforeToNearest.valid = true;
    io.beforeToNearest.processReadMiBps = 80.0;
    classifyStutterIndicators(io);
    check(hasIndicator(io, "IO_SPIKE_INDICATOR"),
          "I/O spike indicator");

    StutterEvent memory{};
    memory.frameTimeMs = 150.0;
    memory.telemetryMatched = true;
    memory.systemMemoryLoadPct = 95.0;
    memory.vramTotalMiB = 8192.0;
    memory.vramUsedMiB = 8000.0;
    classifyStutterIndicators(memory);
    check(hasIndicator(memory, "MEMORY_PRESSURE_INDICATOR"),
          "system memory pressure indicator");
    check(hasIndicator(memory, "VRAM_PRESSURE_INDICATOR"),
          "VRAM pressure indicator");

    StutterEvent presentMode{};
    presentMode.frameTimeMs = 150.0;
    presentMode.presentModeChanged = true;
    classifyStutterIndicators(presentMode);
    check(hasIndicator(presentMode, "PRESENT_MODE_CHANGE_INDICATOR"),
          "present mode transition indicator");

    StutterEvent renderPath{};
    renderPath.frameTimeMs = 300.0;
    renderPath.gpuWaitMs = 220.0;
    renderPath.telemetryMatched = true;
    renderPath.gpuUtilPct = 7.0;
    classifyStutterIndicators(renderPath);
    check(hasIndicator(renderPath, "POSSIBLE_DRIVER_RENDER_PATH_STALL"),
          "render-path wait indicator");

    check(cpu.signals.empty(),
          "diagnostic classification must not pollute low-level signals");

    std::cout << "Stutter indicator tests passed\n";
}
