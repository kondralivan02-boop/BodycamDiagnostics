#pragma once

#include <Windows.h>

constexpr wchar_t kOverlaySharedMemoryName[] =
    L"Local\\BodycamDiagnosticsOverlayState";

struct OverlaySharedState {
    volatile LONG sequence = 0;
    volatile LONG shutdown = 0;
    volatile LONG visible = 0;

    DWORD ownerPid = 0;
    DWORD targetPid = 0;

    LONG frameValid = 0;
    unsigned long long totalFrames = 0;
    double fps = 0.0;
    double frameTimeMs = 0.0;
    double onePercentLowFps = 0.0;
    double pointOnePercentLowFps = 0.0;

    LONG telemetryValid = 0;
    LONG gpuValid = 0;
    double processCpuPct = 0.0;
    double workingSetMiB = 0.0;
    unsigned gpuUtilPct = 0;
    unsigned gpuMemoryUtilPct = 0;
    unsigned long long vramUsedMiB = 0;
    unsigned long long vramTotalMiB = 0;
    unsigned gpuTempC = 0;
    unsigned gpuGraphicsMHz = 0;
    unsigned gpuMemoryMHz = 0;
};
