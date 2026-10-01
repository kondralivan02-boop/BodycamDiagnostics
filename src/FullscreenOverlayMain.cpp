#include <Windows.h>

#include <cstring>

#include "DiagnosticsOverlay.h"
#include "LiveFrameMetrics.h"
#include "OverlaySharedState.h"
#include "TelemetrySampler.h"

namespace {
bool readSharedState(const OverlaySharedState* source,
                     OverlaySharedState& out) {
    if (!source)
        return false;

    for (int attempt = 0; attempt < 4; ++attempt) {
        const LONG before = source->sequence;
        if (before & 1) {
            Sleep(1);
            continue;
        }

        MemoryBarrier();
        std::memcpy(&out, source, sizeof(out));
        MemoryBarrier();

        const LONG after = source->sequence;
        if (before == after && !(after & 1))
            return true;
    }

    return false;
}

bool hasUiAccess() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    DWORD uiAccess = 0;
    DWORD bytes = 0;
    const BOOL ok = GetTokenInformation(
        token,
        TokenUIAccess,
        &uiAccess,
        sizeof(uiAccess),
        &bytes);

    CloseHandle(token);
    return ok && uiAccess != 0;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HANDLE mapping = nullptr;
    for (int attempt = 0; attempt < 50 && !mapping; ++attempt) {
        mapping = OpenFileMappingW(
            FILE_MAP_READ,
            FALSE,
            kOverlaySharedMemoryName);

        if (!mapping)
            Sleep(100);
    }

    if (!mapping)
        return 2;

    const auto* shared = static_cast<const OverlaySharedState*>(
        MapViewOfFile(
            mapping,
            FILE_MAP_READ,
            0, 0,
            sizeof(OverlaySharedState)));

    if (!shared) {
        CloseHandle(mapping);
        return 3;
    }

    DiagnosticsOverlay overlay;
    if (!overlay.create(instance)) {
        UnmapViewOfFile(shared);
        CloseHandle(mapping);
        return 4;
    }

    // The helper is expected to have real Windows UIAccess.  If Windows
    // declined it, exit so the main app can keep using its normal fallback.
    if (!hasUiAccess()) {
        overlay.destroy();
        UnmapViewOfFile(shared);
        CloseHandle(mapping);
        return 5;
    }

    DWORD shownPid = 0;
    HANDLE ownerProcess = nullptr;

    bool running = true;
    while (running) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!running)
            break;

        OverlaySharedState state{};
        if (!readSharedState(shared, state)) {
            Sleep(16);
            continue;
        }

        if (state.shutdown)
            break;

        if (state.ownerPid != 0) {
            if (!ownerProcess) {
                ownerProcess = OpenProcess(
                    SYNCHRONIZE,
                    FALSE,
                    state.ownerPid);
            }

            if (ownerProcess &&
                WaitForSingleObject(ownerProcess, 0) != WAIT_TIMEOUT) {
                break;
            }
        }

        if (state.visible && state.targetPid != 0) {
            if (!overlay.visible() || shownPid != state.targetPid) {
                overlay.showForProcess(state.targetPid);
                shownPid = state.targetPid;
            }

            LiveFrameSnapshot frame{};
            frame.valid = state.frameValid != 0;
            frame.totalFrames =
                static_cast<std::size_t>(state.totalFrames);
            frame.fps = state.fps;
            frame.frameTimeMs = state.frameTimeMs;
            frame.onePercentLowFps = state.onePercentLowFps;
            frame.pointOnePercentLowFps =
                state.pointOnePercentLowFps;

            TelemetrySnapshot telemetry{};
            telemetry.valid = state.telemetryValid != 0;
            telemetry.gpuValid = state.gpuValid != 0;
            telemetry.processCpuPct = state.processCpuPct;
            telemetry.workingSetMiB = state.workingSetMiB;
            telemetry.gpuUtilPct = state.gpuUtilPct;
            telemetry.gpuMemoryUtilPct =
                state.gpuMemoryUtilPct;
            telemetry.vramUsedMiB = state.vramUsedMiB;
            telemetry.vramTotalMiB = state.vramTotalMiB;
            telemetry.gpuTempC = state.gpuTempC;
            telemetry.gpuGraphicsMHz =
                state.gpuGraphicsMHz;
            telemetry.gpuMemoryMHz =
                state.gpuMemoryMHz;

            overlay.update(frame, telemetry);
        } else if (overlay.visible()) {
            overlay.hide();
            shownPid = 0;
        }

        Sleep(50);
    }

    if (ownerProcess)
        CloseHandle(ownerProcess);

    overlay.destroy();
    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    return 0;
}
