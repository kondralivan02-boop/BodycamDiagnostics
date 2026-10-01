#include <Windows.h>
#include <CommCtrl.h>
#include <filesystem>
#include <memory>
#include <string>
#include "SessionManager.h"
#include "ProcessFinder.h"
#include "SystemSnapshot.h"
#include "PresentMonController.h"
#include "RecorderController.h"
#include "ToolPreflight.h"
#include "TelemetrySampler.h"
#include "MetricsCalculator.h"

namespace {
constexpr int ID_STATUS = 1001;
constexpr int ID_START  = 1002;
constexpr int ID_STOP   = 1003;
constexpr int ID_PRESET = 1004;
constexpr int ID_TIMER  = 1;
constexpr int HOTKEY_START = 2001;
constexpr int HOTKEY_STOP  = 2002;

HWND gStatus{};
HWND gStart{};
HWND gStop{};
HWND gPreset{};
std::unique_ptr<SessionManager> gSession;
std::unique_ptr<PresentMonController> gPresentMon;
std::unique_ptr<RecorderController> gRecorder;
std::unique_ptr<TelemetrySampler> gTelemetry;
bool gRunning = false;

std::filesystem::path appDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

void setStatus(const std::wstring& text) {
    SetWindowTextW(gStatus, text.c_str());
    if (gSession) gSession->log(text);
}

void refreshGameState() {
    const auto bodycam = findProcessByName(L"Bodycam-Win64-Shipping.exe");
    if (bodycam.pid) {
        setStatus(L"Bodycam найден. PID: " + std::to_wstring(bodycam.pid));
    } else if (!gRunning) {
        SetWindowTextW(gStatus, L"Ожидание Bodycam...");
    }
}

void startSession() {
    if (gRunning) return;

    const auto bodycam = findProcessByName(L"Bodycam-Win64-Shipping.exe");
    if (!bodycam.pid) {
        MessageBoxW(nullptr, L"Bodycam не найден. Сначала запусти игру.", L"Bodycam Diagnostics", MB_ICONWARNING);
        return;
    }

    wchar_t preset[128]{};
    GetWindowTextW(gPreset, preset, 128);
    std::wstring presetName = preset[0] ? preset : L"Unnamed";

    const auto toolsDir = appDir() / L"Tools";
    const auto preflight = runToolPreflight(toolsDir / L"PresentMon.exe", toolsDir / L"ffmpeg.exe");
    if (!preflight.presentMonFound || !preflight.recordingReady()) {
        std::wstring message = L"Предварительная проверка не пройдена:\n\n" + preflight.details +
            L"\n\nТест не запущен, чтобы не получить неполную сессию.";
        MessageBoxW(nullptr, message.c_str(), L"Bodycam Diagnostics — Preflight", MB_ICONERROR);
        return;
    }

    gSession = std::make_unique<SessionManager>(appDir() / L"Sessions");
    if (!gSession->start(presetName)) {
        MessageBoxW(nullptr, L"Не удалось создать папку сессии.", L"Bodycam Diagnostics", MB_ICONERROR);
        gSession.reset();
        return;
    }

    writeSystemSnapshot(gSession->sessionPath() / L"system.json");
    gSession->log(L"Bodycam PID: " + std::to_wstring(bodycam.pid));

    gTelemetry = std::make_unique<TelemetrySampler>();
    std::wstring telemetryError;
    if (gTelemetry->start(bodycam.pid, gSession->sessionPath() / L"telemetry.csv", &telemetryError)) {
        gSession->log(L"Telemetry capture started");
    } else {
        gSession->log(L"Telemetry unavailable: " + telemetryError);
    }

    gPresentMon = std::make_unique<PresentMonController>(toolsDir / L"PresentMon.exe");
    std::wstring pmError;
    if (gPresentMon->start(bodycam.pid, gSession->sessionPath() / L"presentmon.csv", &pmError)) {
        gSession->log(L"PresentMon capture started");
    } else {
        gSession->log(L"PresentMon unavailable: " + pmError);
    }

    gRecorder = std::make_unique<RecorderController>(toolsDir / L"ffmpeg.exe");
    std::wstring recError;
    if (gRecorder->start(gSession->sessionPath() / L"video", &recError)) {
        gSession->log(L"NVENC recording started");
    } else {
        gSession->log(L"Recorder unavailable: " + recError);
    }

    gRunning = true;
    EnableWindow(gStart, FALSE);
    EnableWindow(gStop, TRUE);
    setStatus(L"Тест запущен. Сессия пишется непрерывно.");
}

void stopSession() {
    if (!gRunning) return;
    if (gRecorder) {
        gRecorder->stop();
        gSession->log(L"NVENC recording stopped");
        gRecorder.reset();
    }
    if (gPresentMon) {
        gPresentMon->stop();
        gSession->log(L"PresentMon capture stopped");
        gPresentMon.reset();
    }
    if (gTelemetry) {
        gTelemetry->stop();
        gSession->log(L"Telemetry capture stopped");
        gTelemetry.reset();
    }

    const auto summary = calculateMetrics(gSession->sessionPath() / L"presentmon.csv");
    if (summary.valid) {
        std::wstring reportError;
        if (writeMetricsReport(summary,
                               gSession->sessionPath() / L"summary.json",
                               gSession->sessionPath() / L"summary.txt",
                               &reportError)) {
            gSession->log(L"Metrics summary written: avg FPS=" +
                          std::to_wstring(summary.averageFps) +
                          L", 1% low=" + std::to_wstring(summary.onePercentLowFps) +
                          L", 0.1% low=" + std::to_wstring(summary.pointOnePercentLowFps));
        } else {
            gSession->log(L"Metrics report failed: " + reportError);
        }
    } else {
        gSession->log(L"Metrics calculation unavailable: " + summary.error);
    }

    setStatus(L"Тест остановлен. Сессия и отчёт сохранены.");
    gSession->stop();
    gRunning = false;
    EnableWindow(gStart, TRUE);
    EnableWindow(gStop, FALSE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        CreateWindowW(L"STATIC", L"Bodycam Diagnostics", WS_CHILD | WS_VISIBLE,
                      20, 15, 420, 28, hwnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"STATIC", L"Пресет:", WS_CHILD | WS_VISIBLE,
                      20, 58, 70, 24, hwnd, nullptr, nullptr, nullptr);
        gPreset = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", nullptr,
                      WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                      95, 55, 390, 180, hwnd, reinterpret_cast<HMENU>(ID_PRESET), nullptr, nullptr);
        SendMessageW(gPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DLSS Quality + Frame Generation OFF"));
        SendMessageW(gPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DLSS Quality + AMD FSR Frame Generation"));
        SendMessageW(gPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DLSS Quality + NVIDIA DLSS Frame Generation 2x"));
        SendMessageW(gPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DLSS Quality + Smooth Motion ON"));
        SendMessageW(gPreset, CB_SETCURSEL, 0, 0);
        gStart = CreateWindowW(L"BUTTON", L"Начать тест", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      20, 100, 140, 34, hwnd, reinterpret_cast<HMENU>(ID_START), nullptr, nullptr);
        gStop = CreateWindowW(L"BUTTON", L"Остановить", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      175, 100, 140, 34, hwnd, reinterpret_cast<HMENU>(ID_STOP), nullptr, nullptr);
        EnableWindow(gStop, FALSE);
        gStatus = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"Ожидание Bodycam...",
                      WS_CHILD | WS_VISIBLE | SS_LEFT,
                      20, 155, 520, 44, hwnd, reinterpret_cast<HMENU>(ID_STATUS), nullptr, nullptr);
        CreateWindowW(L"STATIC",
                      L"F9 Start • F10 Stop • PresentMon + telemetry + NVENC + automatic report",
                      WS_CHILD | WS_VISIBLE,
                      20, 220, 620, 28, hwnd, nullptr, nullptr, nullptr);
        RegisterHotKey(hwnd, HOTKEY_START, MOD_NOREPEAT, VK_F9);
        RegisterHotKey(hwnd, HOTKEY_STOP, MOD_NOREPEAT, VK_F10);
        SetTimer(hwnd, ID_TIMER, 1500, nullptr);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_START: startSession(); return 0;
        case ID_STOP: stopSession(); return 0;
        }
        break;
    case WM_TIMER:
        if (wp == ID_TIMER) refreshGameState();
        return 0;
    case WM_HOTKEY:
        if (wp == HOTKEY_START) { startSession(); return 0; }
        if (wp == HOTKEY_STOP) { stopSession(); return 0; }
        break;
    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER);
        UnregisterHotKey(hwnd, HOTKEY_START);
        UnregisterHotKey(hwnd, HOTKEY_STOP);
        if (gRunning) stopSession();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int show) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    const wchar_t* cls = L"BodycamDiagnosticsWindow";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, cls, L"Bodycam Diagnostics",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 700, 330,
        nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
