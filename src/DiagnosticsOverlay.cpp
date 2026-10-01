#include "DiagnosticsOverlay.h"

#include <algorithm>
#include <cwchar>
#include <string>

namespace {
constexpr wchar_t kOverlayClass[] = L"BodycamDiagnosticsOverlay";
constexpr int kOverlayWidth = 326;
constexpr int kOverlayHeight = 252;

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

void drawTextLine(HDC dc,
                  HFONT font,
                  COLORREF color,
                  int x,
                  int y,
                  int width,
                  int height,
                  const std::wstring& text,
                  UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    SelectObject(dc, font);
    SetTextColor(dc, color);
    RECT r{x, y, x + width, y + height};
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &r, format);
}

std::wstring formatDouble(double value, int decimals) {
    wchar_t buffer[64]{};
    if (decimals == 0)
        swprintf_s(buffer, L"%.0f", value);
    else if (decimals == 1)
        swprintf_s(buffer, L"%.1f", value);
    else
        swprintf_s(buffer, L"%.2f", value);
    return buffer;
}

std::wstring formatVram(unsigned long long usedMiB,
                        unsigned long long totalMiB) {
    wchar_t buffer[96]{};
    if (totalMiB > 0) {
        swprintf_s(buffer, L"%.1f / %.1f GB",
                   usedMiB / 1024.0,
                   totalMiB / 1024.0);
    } else {
        swprintf_s(buffer, L"%.1f GB", usedMiB / 1024.0);
    }
    return buffer;
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

    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED |
        WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        kOverlayClass,
        L"",
        WS_POPUP,
        0, 0, kOverlayWidth, kOverlayHeight,
        nullptr, nullptr, instance, this);

    if (!hwnd_) return false;

    SetLayeredWindowAttributes(hwnd_, 0, 238, LWA_ALPHA);
    ensureFonts();
    return true;
}

void DiagnosticsOverlay::ensureFonts() {
    if (!titleFont_) {
        titleFont_ = CreateFontW(
            -17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

    if (!valueFont_) {
        valueFont_ = CreateFontW(
            -16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

    if (!labelFont_) {
        labelFont_ = CreateFontW(
            -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
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

    const int margin = 18;
    const int x = mi.rcMonitor.right - kOverlayWidth - margin;
    const int y = mi.rcMonitor.top + margin;

    SetWindowPos(
        hwnd_, HWND_TOPMOST,
        x, y, kOverlayWidth, kOverlayHeight,
        SWP_NOACTIVATE);
}

void DiagnosticsOverlay::paint() {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd_, &ps);

    RECT client{};
    GetClientRect(hwnd_, &client);

    HBRUSH background = CreateSolidBrush(RGB(17, 20, 27));
    HPEN border = CreatePen(PS_SOLID, 1, RGB(46, 54, 68));

    const auto oldBrush = SelectObject(dc, background);
    const auto oldPen = SelectObject(dc, border);
    RoundRect(dc, 0, 0, client.right, client.bottom, 16, 16);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(border);
    DeleteObject(background);

    SetBkMode(dc, TRANSPARENT);

    const COLORREF primary = RGB(238, 242, 248);
    const COLORREF muted = RGB(145, 156, 174);
    const COLORREF accent = RGB(85, 150, 255);
    const COLORREF live = RGB(82, 214, 142);

    drawTextLine(dc, titleFont_, primary, 18, 13, 205, 26,
                 L"BODYCAM DIAGNOSTICS");
    drawTextLine(dc, labelFont_, live, 230, 13, 78, 26,
                 L"●  LIVE", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    HPEN divider = CreatePen(PS_SOLID, 1, RGB(42, 49, 62));
    const auto oldDivider = SelectObject(dc, divider);
    MoveToEx(dc, 18, 45, nullptr);
    LineTo(dc, 308, 45);
    SelectObject(dc, oldDivider);
    DeleteObject(divider);

    const std::wstring fps = frame_.valid ? formatDouble(frame_.fps, 0) : L"—";
    const std::wstring ft = frame_.valid ? formatDouble(frame_.frameTimeMs, 1) + L" ms" : L"—";
    const std::wstring low1 = frame_.valid ? formatDouble(frame_.onePercentLowFps, 0) : L"—";
    const std::wstring low01 = frame_.valid ? formatDouble(frame_.pointOnePercentLowFps, 0) : L"—";

    drawTextLine(dc, labelFont_, muted, 18, 55, 58, 20, L"FPS");
    drawTextLine(dc, valueFont_, accent, 76, 54, 72, 22, fps);
    drawTextLine(dc, labelFont_, muted, 158, 55, 72, 20, L"FRAME");
    drawTextLine(dc, valueFont_, primary, 220, 54, 88, 22, ft, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    drawTextLine(dc, labelFont_, muted, 18, 82, 70, 20, L"1% LOW");
    drawTextLine(dc, valueFont_, primary, 82, 81, 62, 22, low1);
    drawTextLine(dc, labelFont_, muted, 158, 82, 72, 20, L"0.1% LOW");
    drawTextLine(dc, valueFont_, primary, 238, 81, 70, 22, low01, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    const std::wstring gpu = telemetry_.gpuValid
        ? std::to_wstring(telemetry_.gpuUtilPct) + L"%"
        : L"—";
    const std::wstring temp = telemetry_.gpuValid
        ? std::to_wstring(telemetry_.gpuTempC) + L" °C"
        : L"—";
    const std::wstring coreClock = telemetry_.gpuValid
        ? std::to_wstring(telemetry_.gpuGraphicsMHz) + L" MHz"
        : L"—";
    const std::wstring memClock = telemetry_.gpuValid
        ? std::to_wstring(telemetry_.gpuMemoryMHz) + L" MHz"
        : L"—";
    const std::wstring vram = telemetry_.gpuValid
        ? formatVram(telemetry_.vramUsedMiB, telemetry_.vramTotalMiB)
        : L"—";
    const std::wstring cpu = telemetry_.valid
        ? formatDouble(telemetry_.processCpuPct, 0) + L"%"
        : L"—";

    drawTextLine(dc, labelFont_, muted, 18, 116, 86, 20, L"GPU");
    drawTextLine(dc, valueFont_, primary, 106, 115, 64, 22, gpu);
    drawTextLine(dc, labelFont_, muted, 180, 116, 56, 20, L"TEMP");
    drawTextLine(dc, valueFont_, primary, 236, 115, 72, 22, temp, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    drawTextLine(dc, labelFont_, muted, 18, 143, 86, 20, L"VRAM");
    drawTextLine(dc, valueFont_, primary, 106, 142, 202, 22, vram, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    drawTextLine(dc, labelFont_, muted, 18, 170, 86, 20, L"GPU CORE");
    drawTextLine(dc, valueFont_, primary, 106, 169, 202, 22, coreClock, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    drawTextLine(dc, labelFont_, muted, 18, 197, 86, 20, L"GPU MEM");
    drawTextLine(dc, valueFont_, primary, 106, 196, 202, 22, memClock, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    drawTextLine(dc, labelFont_, muted, 18, 224, 86, 20, L"CPU GAME");
    drawTextLine(dc, valueFont_, primary, 106, 223, 88, 22, cpu);
    drawTextLine(dc, labelFont_, muted, 205, 224, 103, 20,
                 L"F9  STOP", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    EndPaint(hwnd_, &ps);
}

void DiagnosticsOverlay::destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

    if (titleFont_) {
        DeleteObject(titleFont_);
        titleFont_ = nullptr;
    }
    if (valueFont_) {
        DeleteObject(valueFont_);
        valueFont_ = nullptr;
    }
    if (labelFont_) {
        DeleteObject(labelFont_);
        labelFont_ = nullptr;
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
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
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
