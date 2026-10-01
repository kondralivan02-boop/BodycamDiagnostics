#pragma once

#include <Windows.h>

#include "LiveFrameMetrics.h"
#include "TelemetrySampler.h"

class DiagnosticsOverlay {
public:
    DiagnosticsOverlay() = default;
    ~DiagnosticsOverlay();

    bool create(HINSTANCE instance);
    void showForProcess(DWORD pid);
    void hide();
    void update(const LiveFrameSnapshot& frame,
                const TelemetrySnapshot& telemetry);
    void destroy();

    bool visible() const noexcept { return visible_; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void paint();
    void positionForProcess(DWORD pid);
    void ensureFonts();

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    HFONT valueFont_ = nullptr;
    bool visible_ = false;

    int width_ = 1400;
    int height_ = 30;

    LiveFrameSnapshot frame_;
    TelemetrySnapshot telemetry_;
};
