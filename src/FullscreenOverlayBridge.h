#pragma once

#include <Windows.h>

#include <filesystem>
#include <string>

#include "LiveFrameMetrics.h"
#include "OverlaySharedState.h"
#include "TelemetrySampler.h"

class FullscreenOverlayBridge {
public:
    FullscreenOverlayBridge() = default;
    ~FullscreenOverlayBridge();

    bool initialize(std::wstring* error = nullptr);
    bool ensureHelperRunning(std::wstring* error = nullptr);

    void publish(DWORD targetPid,
                 const LiveFrameSnapshot& frame,
                 const TelemetrySnapshot& telemetry);
    void hide();
    void shutdown();

    bool ready() const noexcept { return state_ != nullptr; }
    static std::filesystem::path installedHelperPath();

private:
    void beginWrite();
    void endWrite();

    HANDLE mapping_ = nullptr;
    OverlaySharedState* state_ = nullptr;
    HANDLE helperProcess_ = nullptr;
};
