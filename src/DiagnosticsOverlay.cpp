#include "DiagnosticsOverlay.h"

#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

namespace {
constexpr wchar_t kOverlayClass[] = L"BodycamDiagnosticsOverlay";
constexpr int kOverlayHeight = 52;
constexpr int kOverlayMinWidth = 900;

HWND findWindowForProcess(DWORD pid) {
    struct Context {
        DWORD pid = 0;
        HWND hwnd = nullptr;
    } context{pid, nullptr};

    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        auto* c = reinterpret_cast<Context*>(param);

        DWORD windowPid = 0;
        GetWindowThreadProcessId(hwnd, &windowPid);
        if (windowPid != c->pid || !IsWindowVisible(hwnd))
            return TRUE;

        if (GetWindow(hwnd, GW_OWNER) != nullptr)
            return TRUE;

        RECT r{};
        GetWindowRect(hwnd, &r);
        if ((r.right - r.left) < 320 || (r.bottom - r.top) < 200)
            return TRUE;

        c->hwnd = hwnd;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&context));

    return context.hwnd;
}

std::wstring fmt(double value, int decimals = 0) {
    wchar_t buffer[64]{};
    if (decimals <= 0)
        swprintf_s(buffer, L"%.0f", value);
    else
        swprintf_s(buffer, L"%.1f", value);
    return buffer;
}

std::wstring vramText(unsigned long long usedMiB,
                      unsigned long long totalMiB) {
    wchar_t buffer[96]{};
    if (totalMiB > 0) {
        swprintf_s(buffer, L"%.1f/%.1f GB",
                   usedMiB / 1024.0,
                   totalMiB / 1024.0);
    } else {
        swprintf_s(buffer, L"%.1f GB", usedMiB / 1024.0);
    }
    return buffer;
}

struct Segment {
    std::wstring label;
    std::wstring value;
    COLORREF valueColor;
};

int textWidth(HDC dc, HFONT font, const std::wstring& text) {
    SelectObject(dc, font);
    SIZE s{};
    GetTextExtentPoint32W(dc, text.c_str(),
                          static_cast<int>(text.size()), &s);
    return s.cx;
}
}

DiagnosticsOverlay::~DiagnosticsOverlay() {
    destroy();
}

bool DiagnosticsOverlay::create(HINSTANCE instance) {
    if (hwnd_) return true;

    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kOverlayClass;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;

    if (!RegisterClassW(&wc) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED |
        WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        kOverlayClass,
        L"",
        WS_POPUP,
        0, 0, width_, height_,
        nullptr, nullptr, instance, this);

    if (!hwnd_) return false;

    SetLayeredWindowAttributes(hwnd_, 0, 232, LWA_ALPHA);
    ensureFonts();
    return true;
}

void DiagnosticsOverlay::ensureFonts() {
    if (!font_) {
        font_ = CreateFontW(
            -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
    }

    if (!valueFont_) {
        valueFont_ = CreateFontW(
            -16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
    }
}

void DiagnosticsOverlay::showForProcess(DWORD pid) {
    if (!hwnd_) return;

    positionForProcess(pid);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetWindowPos(
        hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);

    visible_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DiagnosticsOverlay::hide() {
    if (!hwnd_) return;
    ShowWindow(hwnd_, SW_HIDE);
    visible_ = false;
}

void DiagnosticsOverlay::update(const LiveFrameSnapshot& frame,
                                const TelemetrySnapshot& telemetry) {
    frame_ = frame;
    telemetry_ = telemetry;

    if (hwnd_ && visible_)
        InvalidateRect(hwnd_, nullptr, FALSE);
}

void DiagnosticsOverlay::positionForProcess(DWORD pid) {
    HWND game = findWindowForProcess(pid);
    HMONITOR monitor = MonitorFromWindow(
        game ? game : GetForegroundWindow(),
        MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(monitor, &mi);

    const int monitorWidth = mi.rcMonitor.right - mi.rcMonitor.left;
    const int margin = 14;

    width_ = (std::max)(kOverlayMinWidth, monitorWidth - margin * 2);
    height_ = kOverlayHeight;

    const int x = mi.rcMonitor.left + margin;
    const int y = mi.rcMonitor.top + 10;

    SetWindowPos(
        hwnd_, HWND_TOPMOST,
        x, y, width_, height_,
        SWP_NOACTIVATE);

    HRGN region = CreateRoundRectRgn(
        0, 0, width_ + 1, height_ + 1, 12, 12);
    if (region)
        SetWindowRgn(hwnd_, region, TRUE);
}

void DiagnosticsOverlay::paint() {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd_, &ps);

    RECT client{};
    GetClientRect(hwnd_, &client);

    HBRUSH background = CreateSolidBrush(RGB(13, 16, 22));
    HPEN border = CreatePen(PS_SOLID, 1, RGB(48, 57, 72));

    const auto oldBrush = SelectObject(dc, background);
    const auto oldPen = SelectObject(dc, border);
    RoundRect(dc, 0, 0, client.right, client.bottom, 12, 12);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(border);
    DeleteObject(background);

    SetBkMode(dc, TRANSPARENT);

    const COLORREF text = RGB(232, 237, 244);
    const COLORREF muted = RGB(142, 153, 171);
    const COLORREF green = RGB(77, 220, 127);
    const COLORREF blue = RGB(93, 177, 255);
    const COLORREF amber = RGB(255, 190, 73);
    const COLORREF red = RGB(255, 92, 92);

    const std::wstring fps = frame_.valid ? fmt(frame_.fps) : L"—";
    const std::wstring ft = frame_.valid ? fmt(frame_.frameTimeMs, 1) + L" ms" : L"—";
    const std::wstring low1 = frame_.valid ? fmt(frame_.onePercentLowFps) : L"—";
    const std::wstring low01 = frame_.valid ? fmt(frame_.pointOnePercentLowFps) : L"—";

    const std::wstring gpu = telemetry_.gpuValid
        ? std::to_wstring(telemetry_.gpuUtilPct) + L"%"
        : L"—";
    const std::wstring gpuTemp = telemetry_.gpuValid
        ? std::to_wstring(telemetry_.gpuTempC) + L"°C"
        : L"—";
    const std::wstring core = telemetry_.gpuValid
        ? std::to_wstring(telemetry_.gpuGraphicsMHz) + L" MHz"
        : L"—";
    const std::wstring mem = telemetry_.gpuValid
        ? std::to_wstring(telemetry_.gpuMemoryMHz) + L" MHz"
        : L"—";
    const std::wstring vram = telemetry_.gpuValid
        ? vramText(telemetry_.vramUsedMiB, telemetry_.vramTotalMiB)
        : L"—";
    const std::wstring cpu = telemetry_.valid
        ? fmt(telemetry_.processCpuPct) + L"%"
        : L"—";

    std::vector<Segment> segments = {
        {L"FPS", fps, green},
        {L"FT", ft, green},
        {L"1%", low1, green},
        {L"0.1%", low01, red},
        {L"GPU", gpu, red},
        {L"TEMP", gpuTemp, amber},
        {L"CORE", core, green},
        {L"VRAM", vram, text},
        {L"CPU", cpu, blue},
        {L"MEM", mem, green},
    };

    int x = 18;
    const int y = 0;
    const int lineHeight = client.bottom;

    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto& s = segments[i];

        SelectObject(dc, font_);
        SetTextColor(dc, muted);
        RECT labelRect{x, y, x + 80, lineHeight};
        DrawTextW(dc, s.label.c_str(), -1, &labelRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const int labelW = textWidth(dc, font_, s.label);
        x += labelW + 7;

        SelectObject(dc, valueFont_);
        SetTextColor(dc, s.valueColor);
        const int valueW = textWidth(dc, valueFont_, s.value);
        RECT valueRect{x, y, x + valueW + 2, lineHeight};
        DrawTextW(dc, s.value.c_str(), -1, &valueRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        x += valueW + 16;

        if (i + 1 < segments.size()) {
            SetTextColor(dc, RGB(69, 78, 94));
            SelectObject(dc, font_);
            RECT sep{x, y, x + 12, lineHeight};
            DrawTextW(dc, L"|", -1, &sep,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            x += 16;
        }
    }

    SelectObject(dc, font_);
    SetTextColor(dc, muted);
    RECT stopRect{
        (std::max)(x + 8, static_cast<int>(client.right) - 86),
        0,
        client.right - 14,
        lineHeight
    };
    DrawTextW(dc, L"F9 STOP", -1, &stopRect,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    EndPaint(hwnd_, &ps);
}

void DiagnosticsOverlay::destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

    if (font_) {
        DeleteObject(font_);
        font_ = nullptr;
    }

    if (valueFont_) {
        DeleteObject(valueFont_);
        valueFont_ = nullptr;
    }

    visible_ = false;
}

LRESULT CALLBACK DiagnosticsOverlay::WindowProc(
    HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {

    DiagnosticsOverlay* self =
        reinterpret_cast<DiagnosticsOverlay*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<DiagnosticsOverlay*>(cs->lpCreateParams);

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self));
    }

    switch (msg) {
    case WM_PAINT:
        if (self) {
            self->paint();
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_NCHITTEST:
        return HTTRANSPARENT;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}
