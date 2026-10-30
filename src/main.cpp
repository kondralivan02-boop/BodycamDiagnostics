#include <Windows.h>
#include <CommCtrl.h>
#include <Shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "AppStorage.h"
#ifdef BODYCAM_EXPERIMENTAL_AUTOTUNE
#include "AutoTuneWindows.h"
#endif
#ifdef BODYCAM_SINGLE_EXE
#include "EmbeddedRuntime.h"
#endif
#include "AudioLoopbackRecorder.h"
#include "AudioVideoMuxer.h"
#include "CrashCollector.h"
#include "CaptureStartupPolicy.h"
#include "CollectionPolicy.h"
#include "BodycamLogs.h"
#include "EnvironmentDiagnostics.h"
#include "DiagnosticsOverlay.h"
#include "FullscreenOverlayBridge.h"
#include "LiveFrameMetrics.h"
#include "MetricsCalculator.h"
#include "PerformanceHealth.h"
#include "PresentMonController.h"
#include "ProcessFinder.h"
#include "RecorderController.h"
#include "RecordingTimeline.h"
#include "ReportPackager.h"
#include "SessionManager.h"
#include "SessionDiagnostics.h"
#include "SessionMirror.h"
#include "SystemSnapshot.h"
#include "StutterCorrelator.h"
#include "TelemetrySampler.h"
#include "TestValidator.h"
#include "ToolPreflight.h"

namespace {
constexpr int ID_TOGGLE = 1001;
constexpr int ID_PRESET = 1002;
constexpr int ID_OPEN_SESSION = 1003;
constexpr int ID_COPY_REPORT = 1004;
constexpr int ID_STATUS = 1005;
constexpr int ID_LOG = 1006;
constexpr int ID_RECORD = 1007;
constexpr int ID_VIDEO_STATUS = 1008;
constexpr int ID_GIZMO = 1009;
constexpr int ID_HELP = 1010;
constexpr int ID_NAV_HOME = 1011;
constexpr int ID_NAV_PRESET = 1012;
constexpr int ID_NAV_RECORD = 1013;
constexpr int ID_NAV_REPORTS = 1014;
constexpr int ID_NAV_SETTINGS = 1015;
constexpr int ID_CLEAR_LOG = 1016;
constexpr int ID_HELP_TEXT = 1017;
constexpr int ID_LICENSES = 1018;
#ifdef BODYCAM_EXPERIMENTAL_AUTOTUNE
constexpr int ID_AUTOTUNE_RUN = 1020;
constexpr int ID_AUTOTUNE_RESTORE = 1021;
#endif
constexpr int ID_TIMER = 1;
constexpr int HOTKEY_TOGGLE = 2001;
constexpr int HOTKEY_RECORD = 2002;

// Intentionally neutral palette: color is reserved for actual running
// diagnostics (green), active video capture (red), and failures.
constexpr COLORREF COLOR_BG = RGB(19, 21, 24);
constexpr COLORREF COLOR_PANEL = RGB(24, 27, 30);
constexpr COLORREF COLOR_PANEL_HOVER = RGB(43, 46, 50);
constexpr COLORREF COLOR_BORDER = RGB(55, 59, 64);
constexpr COLORREF COLOR_TEXT = RGB(234, 236, 238);
constexpr COLORREF COLOR_MUTED = RGB(154, 159, 165);
constexpr COLORREF COLOR_ACCENT = RGB(92, 205, 142);
constexpr COLORREF COLOR_ACCENT_PRESSED = RGB(70, 175, 116);
constexpr COLORREF COLOR_STOP = RGB(198, 76, 88);
constexpr COLORREF COLOR_DISABLED = RGB(38, 40, 44);
constexpr COLORREF COLOR_OK = RGB(92, 205, 142);
constexpr COLORREF COLOR_AMBER = RGB(210, 161, 96);
constexpr int kNavTop = 12;
constexpr int kNavHeight = 42;
constexpr int kConfigTop = 70;
constexpr int kActionsTop = 138;
constexpr int kMetricsTop = 267;
constexpr int kLogTop = 375;
constexpr int kFooterHeight = 69;


HWND gMain{};
HWND gPreset{};
HWND gToggle{};
HWND gOpenSession{};
HWND gCopyReport{};
HWND gStatus{};
HWND gLog{};
HWND gRecord{};
HWND gVideoStatus{};
HWND gGizmo{};
HWND gHelp{};
HWND gNavHome{};
HWND gNavPreset{};
HWND gNavRecord{};
HWND gNavReports{};
HWND gNavSettings{};
HWND gClearLog{};
bool gGizmoMode = false;

HFONT gTitleFont{};
HFONT gSubtitleFont{};
HFONT gBodyFont{};
HFONT gSmallFont{};
HFONT gButtonFont{};

HBRUSH gBackgroundBrush{};
HBRUSH gPanelBrush{};

std::unique_ptr<SessionManager> gSession;
std::unique_ptr<PresentMonController> gPresentMon;
std::unique_ptr<RecorderController> gRecorder;
std::unique_ptr<TelemetrySampler> gTelemetry;
std::unique_ptr<SessionMirror> gMirror;
std::unique_ptr<AudioLoopbackRecorder> gAudio;
std::vector<bool> gPartAudioCaptured;
RecordingTimeline gRecording;

DiagnosticsOverlay gOverlay;
FullscreenOverlayBridge gFullscreenOverlay;
LiveFrameMetrics gLiveFrames;
LiveFrameSnapshot gDashboardFrame;
TelemetrySnapshot gDashboardTelemetry;
bool gFullscreenHudActive = false;

std::filesystem::path gLastSessionPath;
std::filesystem::path gLastReportPath;
std::filesystem::path gRuntimeFfmpegExe;
std::filesystem::path gRuntimeRoot;
std::filesystem::path gSessionRoot;

DWORD gGamePid = 0;
bool gRunning = false;
bool gGameExitedUnexpectedly = false;
std::chrono::steady_clock::time_point gSessionStartedAt{};
std::chrono::system_clock::time_point gSessionStartedWallClock{};
std::chrono::system_clock::time_point gPresentMonStartedWallClock{};
std::chrono::system_clock::time_point gRecorderStartedWallClock{};
bool gRecorderStartObserved = false;
bool gRecorderTimelineContinuous = false;
EnvironmentCapabilities gEnvironmentCapabilities;
unsigned gIdleTick = 0;

std::filesystem::path appDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

std::filesystem::path runtimeDir() {
    return gRuntimeRoot.empty() ? appDir() : gRuntimeRoot;
}

std::wstring uiTimestamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

void appendUiLog(const std::wstring& message) {
    if (!gLog) return;

    const std::wstring line = uiTimestamp() + L"   " + message + L"\r\n";
    const int length = GetWindowTextLengthW(gLog);
    SendMessageW(gLog, EM_SETSEL, length, length);
    SendMessageW(gLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
    SendMessageW(gLog, EM_SCROLLCARET, 0, 0);
}

void logEvent(const std::wstring& message) {
    appendUiLog(message);
    if (gSession)
        gSession->log(message);
}

void setStatusVisual(const std::wstring& text) {
    if (gStatus)
        SetWindowTextW(gStatus, text.c_str());
}

void setStatus(const std::wstring& text) {
    setStatusVisual(text);
    logEvent(text);
}

std::filesystem::path configuredMirrorRoot() {
    wchar_t buffer[2048]{};
    const auto ini = appDir() / L"settings.ini";

    GetPrivateProfileStringW(
        L"storage", L"mirror_path", L"",
        buffer, static_cast<DWORD>(std::size(buffer)), ini.c_str());

    if (buffer[0] != L'\0')
        return std::filesystem::path(buffer);

    const DWORD count = GetEnvironmentVariableW(
        L"BODYCAM_DIAG_MIRROR", buffer, static_cast<DWORD>(std::size(buffer)));

    if (count > 0 && count < std::size(buffer))
        return std::filesystem::path(buffer);

    return {};
}

bool copyTextToClipboard(HWND owner, const std::wstring& text) {
    if (text.empty() || !OpenClipboard(owner))
        return false;

    EmptyClipboard();

    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        CloseClipboard();
        return false;
    }

    void* target = GlobalLock(memory);
    if (!target) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }

    CopyMemory(target, text.c_str(), bytes);
    GlobalUnlock(memory);

    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

void updateControlsForRunningState() {
    if (!gToggle) return;

    // State controls the color. Hotkey failure must never imply a running
    // session or a recording that failed to start.
    SetWindowTextW(gToggle, gRunning ? L"■  F9  ОСТАНОВИТЬ"
                                    : L"▶  F9  НАЧАТЬ ТЕСТ");
    const bool recording = gRecorder && gRecorder->running();
    if (gRecord) {
        SetWindowTextW(gRecord, recording ? L"■  F8  СТОП ЗАПИСИ"
                                          : L"●  F8  ЗАПИСЬ ВИДЕО");
        EnableWindow(gRecord, gRunning);
        InvalidateRect(gRecord, nullptr, FALSE);
    }
    if (gVideoStatus) {
        const wchar_t* label = L"Видео выключено";
        if (gRecording.state() == RecordingState::Starting) label = L"Запуск записи";
        if (gRecording.state() == RecordingState::Recording) label = L"REC  •  Идёт запись";
        if (gRecording.state() == RecordingState::Lost) label = L"Ошибка видеозахвата";
        if (gRecording.state() == RecordingState::Stopped) label = L"Видео остановлено";
        SetWindowTextW(gVideoStatus, label);
    }
    EnableWindow(gPreset, !gRunning);
    if (gGizmo) EnableWindow(gGizmo, !gRunning);
    EnableWindow(gOpenSession, !gRunning && !gLastSessionPath.empty());
    EnableWindow(gCopyReport, !gRunning && !gLastReportPath.empty());
    if (!gRunning) {
        gDashboardFrame = {};
        gDashboardTelemetry = {};
    }
    InvalidateRect(gToggle, nullptr, FALSE);
    if (gMain) InvalidateRect(gMain, nullptr, FALSE);
}

void refreshGameState() {
    if (gRunning) return;

    const auto bodycam = findProcessByName(L"Bodycam-Win64-Shipping.exe");
    if (bodycam.pid) {
        gGamePid = bodycam.pid;
        setStatusVisual(L"Bodycam найден  •  PID " + std::to_wstring(bodycam.pid));
    } else {
        gGamePid = 0;
        setStatusVisual(L"Ожидание Bodycam...");
    }
}

void abortSessionStartup(const std::wstring& reason) {
    logEvent(L"Startup aborted  •  " + reason);

    if (gRecorder) {
        gRecorder->stop();
        gRecorder.reset();
    }

    if (gAudio) {
        gAudio->stop();
        gAudio.reset();
    }

    if (gPresentMon) {
        gPresentMon->stop();
        gPresentMon.reset();
    }

    if (gTelemetry) {
        gTelemetry->stop();
        gTelemetry.reset();
    }

    std::filesystem::path failedSessionPath;
    if (gSession) {
        failedSessionPath = gSession->sessionPath();
        gSession->stop();
    }

    if (gMirror) {
        gMirror->stop();
        gMirror.reset();
    }

    gLastSessionPath = failedSessionPath;
    gLastReportPath.clear();
    gSession.reset();

    gGamePid = 0;
    gRunning = false;
    gRecorderStartObserved = false;
    gRecorderTimelineContinuous = false;

    updateControlsForRunningState();
    setStatusVisual(
        L"Тест не запущен  •  обязательный источник недоступен");

    std::wstring message =
        L"Тест не запущен, потому что обязательный источник "
        L"диагностики не стартовал:\n\n" +
        reason;

    if (!failedSessionPath.empty()) {
        message +=
            L"\n\nДиагностический лог запуска сохранён:\n" +
            failedSessionPath.wstring();
    }

    MessageBoxW(
        gMain,
        message.c_str(),
        L"Bodycam Diagnostics — Startup",
        MB_ICONERROR);
}

void startSession() {
    if (gRunning) return;

    const auto bodycam = findProcessByName(L"Bodycam-Win64-Shipping.exe");
    if (!bodycam.pid) {
        MessageBoxW(
            gMain,
            L"Bodycam не найден. Сначала запусти игру.",
            L"Bodycam Diagnostics",
            MB_ICONWARNING);
        return;
    }

    wchar_t preset[160]{};
    GetWindowTextW(gPreset, preset, static_cast<int>(std::size(preset)));
    const std::wstring presetName = preset[0] ? preset : L"Unnamed";

    const auto runtimeRoot = runtimeDir();
    const auto presentMonExe =
        resolveRuntimeTool(runtimeRoot, L"PresentMon.exe");
    const auto ffmpegExe =
        resolveRuntimeTool(runtimeRoot, L"ffmpeg.exe");

    gRuntimeFfmpegExe = ffmpegExe;

    const auto preflight = runToolPreflight(
        presentMonExe,
        ffmpegExe,
        runtimeRoot / L"runtime-tools.sha256",
        false);

    if (!preflight.hashManifestTrusted ||
        !preflight.presentMonFound ||
        !preflight.presentMonTrusted) {
        const std::wstring message =
            L"Предварительная проверка не пройдена:\n\n" +
            preflight.details +
            L"\n\nТест не запущен, чтобы не получить неполную сессию.";

        MessageBoxW(
            gMain,
            message.c_str(),
            L"Bodycam Diagnostics — Preflight",
            MB_ICONERROR);
        return;
    }

    gSession =
        std::make_unique<SessionManager>(
            gSessionRoot);

    std::wstring sessionStartError;
    if (!gSession->start(
            presetName,
            &sessionStartError)) {
        const std::wstring message =
            L"Не удалось создать диагностическую сессию:\n\n" +
            (sessionStartError.empty()
                ? std::wstring(L"Unknown session initialization failure")
                : sessionStartError);

        MessageBoxW(
            gMain,
            message.c_str(),
            L"Bodycam Diagnostics — Session",
            MB_ICONERROR);
        gSession.reset();
        return;
    }

    gGamePid = bodycam.pid;
    gGameExitedUnexpectedly = false;
    gSessionStartedAt = std::chrono::steady_clock::now();
    gSessionStartedWallClock = std::chrono::system_clock::now();
    gRecorderStartObserved = false;
    gRecorderTimelineContinuous = false;
    gRecording = RecordingTimeline{};
    gPartAudioCaptured.clear();
    gLastSessionPath.clear();
    gLastReportPath.clear();

    if (writeSystemSnapshot(
            gSession->sessionPath() / L"system.json")) {
        logEvent(L"System snapshot  •  saved");
    } else {
        logEvent(L"System snapshot недоступен");
    }

    logEvent(L"Сессия создана  •  " + presetName);
    logEvent(L"Файлы сессии  •  " + gSession->sessionPath().wstring());
    logEvent(L"Bodycam PID  •  " + std::to_wstring(bodycam.pid));

    gGizmoMode = SendMessageW(gGizmo, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const auto collection = CollectionPolicy::forGizmo(gGizmoMode);
    gEnvironmentCapabilities = probeEnvironmentCapabilities(bodycam.pid, collection.windowsEvents);
    if (!saveEnvironmentCapabilities(gEnvironmentCapabilities, gSession->sessionPath() / L"environment_capabilities.json"))
        logEvent(L"Cannot save environment capabilities profile");

    const auto mirrorRoot = configuredMirrorRoot();
    const bool mirrorConfigured = !mirrorRoot.empty();
    bool mirrorStarted = false;
    std::wstring mirrorError;

    if (mirrorConfigured) {
        gMirror = std::make_unique<SessionMirror>(
            gSession->sessionPath(), mirrorRoot);

        if (gMirror->start(&mirrorError)) {
            mirrorStarted = true;
            logEvent(L"Persistent mirror  •  " + gMirror->targetPath().wstring());
        } else {
            logEvent(L"Persistent mirror недоступен  •  " + mirrorError);
            gMirror.reset();
        }
    }

    gTelemetry = std::make_unique<TelemetrySampler>();
    std::wstring telemetryError;
    if (gTelemetry->start(
            bodycam.pid,
            gSession->sessionPath() / L"telemetry.csv",
            &telemetryError)) {
        gEnvironmentCapabilities.sources.push_back({"telemetry", "started", 0, true, "Startup succeeded; completeness validated separately"});
        logEvent(L"Телеметрия  •  ON");
    } else {
        gEnvironmentCapabilities.sources.push_back({"telemetry", "unavailable", 0, true, "See session.log for startup error; no Win32 code supplied by controller"});
        logEvent(L"Телеметрия недоступна  •  " + telemetryError);
    }

    gPresentMon = std::make_unique<PresentMonController>(
        presentMonExe);

    gPresentMonStartedWallClock = std::chrono::system_clock::now();
    std::wstring pmError;
    const bool presentMonStarted =
        gPresentMon->start(
            bodycam.pid,
            gSession->sessionPath() / L"presentmon.csv",
            &pmError);

    if (presentMonStarted) {
        gEnvironmentCapabilities.sources.push_back({"presentmon", "started", 0, false, "Startup succeeded; completeness validated separately"});
        logEvent(L"PresentMon  •  ON");
        gLiveFrames.reset(gSession->sessionPath() / L"presentmon.csv");
    } else {
        gEnvironmentCapabilities.sources.push_back({"presentmon", "unavailable", 0, false, "See session.log for startup error; no Win32 code supplied by controller"});
        logEvent(L"PresentMon недоступен  •  " + pmError);
    }

    gEnvironmentCapabilities.sources.push_back({"nvenc_video", "disabled", 0, true, "Video recording is off until explicitly enabled"});

    if (!requiredCaptureSourcesReady(
            presentMonStarted,
            mirrorConfigured,
            mirrorStarted)) {

        if (!saveEnvironmentCapabilities(
                gEnvironmentCapabilities,
                gSession->sessionPath() /
                    L"environment_capabilities.json")) {
            logEvent(
                L"Cannot save environment capabilities profile");
        }

        std::wstring failure;
        if (!presentMonStarted) {
            failure += L"PresentMon: ";
            failure +=
                pmError.empty()
                    ? L"startup failed"
                    : pmError;
        }

        if (mirrorConfigured && !mirrorStarted) {
            if (!failure.empty())
                failure += L"\n";
            failure += L"Persistent mirror: ";
            failure +=
                mirrorError.empty()
                    ? L"startup failed"
                    : mirrorError;
        }

        abortSessionStartup(failure);
        return;
    }

    gEnvironmentCapabilities.sources.push_back({"wasapi_audio", "disabled", 0, true, "Audio starts with optional video recording"});

    if (!saveEnvironmentCapabilities(gEnvironmentCapabilities, gSession->sessionPath() / L"environment_capabilities.json"))
        logEvent(L"Cannot save environment capabilities profile");

    gRunning = true;
    updateControlsForRunningState();

    std::wstring fullscreenHudError;
#ifdef BODYCAM_SINGLE_EXE
    gFullscreenHudActive = false;
    fullscreenHudError = L"Однофайловая версия использует оконный/Borderless HUD; подписанный UIAccess helper не включён";
#else
    gFullscreenHudActive = collection.fullscreenHelper &&
        gFullscreenOverlay.ensureHelperRunning(&fullscreenHudError);
#endif

    if (gFullscreenHudActive) {
        gOverlay.hide();
        logEvent(L"Fullscreen HUD  •  ON");
    } else {
        if (collection.fullscreenHelper) {
            gOverlay.showForProcess(bodycam.pid);
            logEvent(L"Fullscreen HUD недоступен  •  " + fullscreenHudError);
            logEvent(L"Fallback HUD  •  работает в окне/Borderless");
        } else {
            gEnvironmentCapabilities.sources.push_back({"fullscreen_helper", "disabled_by_gizmo_mode", 0, true, "Gizmo compatibility mode"});
            logEvent(L"Режим Gizmo  •  полноэкранный помощник выключен");
        }
    }

    setStatus(L"Тест идёт  •  F9 для остановки");
}

void stopSession() {
    if (!gRunning || !gSession)
        return;

    const auto observationEnd = std::chrono::system_clock::now();
    const double observationDuration = std::chrono::duration<double>(std::chrono::steady_clock::now() - gSessionStartedAt).count();
    const double wallDuration = std::chrono::duration<double>(observationEnd - gSessionStartedWallClock).count();
    const bool clockStable = std::abs(wallDuration - observationDuration) < 1.0;
    const double videoStartAfterPresentMonMs =
        gRecorderStartObserved
            ? std::chrono::duration<double, std::milli>(
                  gRecorderStartedWallClock -
                  gPresentMonStartedWallClock).count()
            : 0.0;
    const bool videoBookmarkAlignmentAvailable =
        clockStable &&
        gRecorderStartObserved &&
        gRecorderTimelineContinuous &&
        std::isfinite(videoStartAfterPresentMonMs) &&
        videoStartAfterPresentMonMs >= 0.0;

    gFullscreenOverlay.hide();
    gFullscreenHudActive = false;
    gOverlay.hide();
    setStatusVisual(L"Останавливаю тест и формирую отчёт...");

    if (gRecorder) {
        gRecorder->stop();
        gRecording.stop(std::chrono::steady_clock::now());
        logEvent(L"NVENC video  •  OFF");
        gRecorder.reset();
    }

    if (gAudio) {
        std::wstring audioStopError;
        const bool audioFinalized = gAudio->stop(&audioStopError);
        if (!gPartAudioCaptured.empty())
            gPartAudioCaptured.back() = audioFinalized;

        if (audioFinalized) {
            logEvent(L"Game audio  •  OFF");
        } else {
            logEvent(
                L"Game audio incomplete  •  " +
                (audioStopError.empty()
                    ? std::wstring(L"WASAPI finalization failed")
                    : audioStopError));

            gEnvironmentCapabilities.sources.push_back({
                "wasapi_audio_runtime",
                "failed_on_stop",
                0,
                true,
                "Optional WASAPI loopback did not finalize cleanly; final video excludes audio"
            });

            std::error_code audioEc;
            std::filesystem::remove(
                recordingPartAudioPath(
                    gSession->sessionPath(),
                    static_cast<unsigned>(gRecording.intervals().size())),
                audioEc);
        }

        gAudio.reset();
    }

    if (gPresentMon) {
        gPresentMon->stop();
        logEvent(L"PresentMon  •  OFF");
        gPresentMon.reset();
    }

    if (gTelemetry) {
        const auto observed = gTelemetry->latest();
        gEnvironmentCapabilities.sources.push_back({"telemetry_last_sample", observed.valid ? "observed" : "unavailable", 0, true, "Last sampler snapshot; no guarantee of continuous coverage"});
        gEnvironmentCapabilities.sources.push_back({"gpu_last_sample", observed.gpuValid ? "observed" : "unavailable", 0, true, "NVIDIA NVML board telemetry; optional and not available on non-NVIDIA GPUs"});
        gEnvironmentCapabilities.sources.push_back({"process_gpu_3d_telemetry", observed.processGpu3dValid ? "observed" : "unavailable", 0, true, "Per-process maximum 3D engine utilization via Windows PDH; optional WDDM counter"});
        gEnvironmentCapabilities.sources.push_back({"process_max_thread_cpu_telemetry", observed.processMaxThreadCpuValid ? "observed" : "unavailable", 0, true, "Maximum observed CPU utilization of one process thread; optional"});
        gTelemetry->stop();
        logEvent(L"Телеметрия  •  OFF");
        gTelemetry.reset();
    }

    const auto sessionPath = gSession->sessionPath();
    {
        std::ofstream intervals(sessionPath / L"recording_intervals.json", std::ios::binary);
        intervals << "{\"state\":\"";
        switch (gRecording.state()) {
        case RecordingState::Disabled: intervals << "disabled"; break;
        case RecordingState::Starting: intervals << "starting"; break;
        case RecordingState::Recording: intervals << "recording"; break;
        case RecordingState::Lost: intervals << "lost"; break;
        case RecordingState::Stopped: intervals << "stopped"; break;
        }
        intervals << "\",\"had_failure\":" << (gRecording.hadFailure() ? "true" : "false")
                  << ",\"requested_seconds\":" << gRecording.requestedSeconds() << ",\"intervals\":[";
        bool first = true;
        for (const auto& interval : gRecording.intervals()) {
            if (!first) intervals << ',';
            first = false;
            intervals << "{\"start_seconds\":" << std::chrono::duration<double>(interval.start - gSessionStartedAt).count()
                      << ",\"end_seconds\":" << std::chrono::duration<double>(interval.end - gSessionStartedAt).count() << '}';
        }
        intervals << "]}\n";
    }
    const double sessionDurationSeconds =
        std::chrono::duration<double>(
            std::chrono::steady_clock::now() - gSessionStartedAt).count();

    const auto runtimeManifest =
        runtimeDir() / L"runtime-tools.sha256";
    const char* noVideoDetail = gRecording.hadFailure()
        ? "Recording was requested but did not start"
        : "Video was disabled";
    const bool ffmpegTrustedForFinalization =
        !gRecording.intervals().empty() && runtimeToolTrusted(
            gRuntimeFfmpegExe,
            runtimeManifest);

    gEnvironmentCapabilities.sources.push_back({
        "ffmpeg_finalization_trust",
        gRecording.intervals().empty() ? "skipped" : (ffmpegTrustedForFinalization ? "verified" : "failed"),
        0,
        true,
        gRecording.intervals().empty() ? noVideoDetail : (ffmpegTrustedForFinalization
            ? "FFmpeg SHA-256 revalidated immediately before finalization"
            : "FFmpeg or runtime manifest changed after startup; external finalization blocked")
    });

    std::wstring muxError;
    if (gRecording.intervals().empty()) {
        logEvent(gRecording.hadFailure()
            ? L"Видео: запись запрашивалась, но ни один интервал не начался"
            : L"Видео выключено  •  обработка не требуется");
    } else if (!ffmpegTrustedForFinalization) {
        logEvent(
            L"Финализация видео заблокирована  •  "
            L"FFmpeg trust revalidation failed; raw capture preserved");
    } else {
        for (std::size_t index = 0; index < gRecording.intervals().size(); ++index) {
            muxError.clear();
            const unsigned part = static_cast<unsigned>(index + 1);
            if (finalizeRecordingPart(
                    gRuntimeFfmpegExe, sessionPath, part,
                    index < gPartAudioCaptured.size() && gPartAudioCaptured[index],
                    &muxError)) {
                logEvent(L"Видео: фрагмент " + std::to_wstring(part) + L" сохранён отдельно");
                if (!muxError.empty()) logEvent(L"Предупреждение видео  •  " + muxError);
            } else {
                logEvent(L"Видео: не удалось собрать фрагмент " + std::to_wstring(part) + L"  •  " + muxError);
            }
        }
    }

    const auto summary = calculateMetrics(
        sessionPath / L"presentmon.csv");

    if (summary.valid) {
        std::wstring reportError;
        if (writeMetricsReport(
                summary,
                sessionPath / L"summary.json",
                sessionPath / L"summary.txt",
                &reportError)) {
            std::wstringstream metrics;
            metrics << std::fixed << std::setprecision(1)
                    << L"Итог  •  "
                    << summary.averageFps << L" FPS  •  "
                    << summary.onePercentLowFps << L" 1% low";
            logEvent(metrics.str());
        } else {
            logEvent(L"Ошибка итогового отчёта  •  " + reportError);
        }
    } else {
        logEvent(L"Метрики недоступны  •  " + summary.error);
    }

    const auto stutterCorrelation = correlateStutterEvents(
        sessionPath / L"presentmon.csv",
        sessionPath / L"telemetry.csv",
        100.0,
        videoBookmarkAlignmentAvailable,
        videoStartAfterPresentMonMs);

    std::wstring stutterReportError;
    if (writeStutterCorrelationReport(
            stutterCorrelation,
            sessionPath / L"stutter_events.json",
            sessionPath / L"stutter_events.txt",
            &stutterReportError)) {
        logEvent(
            L"Stutter correlation  •  " +
            std::to_wstring(stutterCorrelation.events.size()) +
            L" event(s) >=100 ms");
    } else {
        logEvent(L"Ошибка stutter correlation  •  " + stutterReportError);
    }

    const auto performanceHealth = analyzePerformanceHealth(summary);
    std::wstring performanceHealthError;
    if (writePerformanceHealthReport(
            performanceHealth,
            sessionPath / L"performance_health.json",
            sessionPath / L"performance_health.txt",
            &performanceHealthError)) {
        logEvent(
            performanceHealth.clean
                ? L"Performance health  •  CLEAN"
                : L"Performance health  •  CONTAMINATED");
        for (const auto& finding : performanceHealth.findings)
            logEvent(L"Performance  •  " + finding);
    } else {
        logEvent(L"Ошибка performance health report  •  " + performanceHealthError);
    }

    if (gGameExitedUnexpectedly && !gGizmoMode) {
        const auto crashData = collectRecentBodycamCrashData(
            sessionPath,
            gSessionStartedWallClock);

        if (crashData.copiedAnything)
            logEvent(L"Crash diagnostics  •  " + crashData.details);
        else
            logEvent(L"Crash diagnostics  •  " + crashData.details);
    }

    const bool ffmpegTrustedForProbe =
        !gRecording.intervals().empty() && runtimeToolTrusted(
            gRuntimeFfmpegExe,
            runtimeManifest);

    gEnvironmentCapabilities.sources.push_back({
        "ffmpeg_validation_trust",
        gRecording.intervals().empty() ? "skipped" : (ffmpegTrustedForProbe ? "verified" : "failed"),
        0,
        true,
        gRecording.intervals().empty() ? noVideoDetail : (ffmpegTrustedForProbe
            ? "FFmpeg SHA-256 revalidated immediately before duration probe"
            : "FFmpeg or runtime manifest changed after finalization; duration probe blocked")
    });

    std::vector<double> requestedVideoIntervals;
    for (const auto& interval : gRecording.intervals())
        requestedVideoIntervals.push_back(std::chrono::duration<double>(interval.end - interval.start).count());
    const auto validation = validateTestSession(
        sessionPath,
        summary,
        sessionDurationSeconds,
        gGameExitedUnexpectedly,
        ffmpegTrustedForProbe,
        gRecording.requestedSeconds(),
        gRecording.hadFailure(),
        requestedVideoIntervals,
        gRuntimeFfmpegExe);

    std::wstring validationError;
    if (writeTestValidationReport(
            validation,
            sessionPath / L"validation.json",
            sessionPath / L"validation.txt",
            &validationError)) {
        if (validation.valid) {
            logEvent(L"Проверка теста  •  VALID");
        } else {
            logEvent(
                L"Проверка теста  •  INVALID  •  " +
                std::to_wstring(validation.issues.size()) +
                L" issue(s)");
            for (const auto& issue : validation.issues)
                logEvent(L"Validation  •  " + issue);
        }
    } else {
        logEvent(L"Ошибка validation report  •  " + validationError);
    }

    wchar_t localAppData[32768]{};
    const DWORD localAppDataSize = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    const std::filesystem::path bodycamLogRoot = localAppDataSize > 0 && localAppDataSize < std::size(localAppData)
        ? std::filesystem::path(localAppData) / L"Bodycam" / L"Saved" / L"Logs" : std::filesystem::path{};
    BodycamLogCollection bodycamLogs;
    if (gGizmoMode) {
        bodycamLogs.source = {"bodycam_unreal_logs", "disabled_by_gizmo_mode", 0, true, "Gizmo compatibility mode"};
    } else {
        bodycamLogs = collectBodycamLogs(bodycamLogRoot, sessionPath, gSessionStartedWallClock);
    }
    const bool bodycamManifestWritten = writeBodycamLogsManifest(bodycamLogs, sessionPath / L"bodycam_logs_manifest.json");
    gEnvironmentCapabilities.sources.push_back(bodycamLogs.source);
    logEvent(bodycamManifestWritten ? L"Optional Bodycam log manifest saved" : L"Cannot save optional Bodycam log manifest");

    EventLogCollection windowsEvents;
    if (!gGizmoMode) {
        windowsEvents = collectWindowsEvents(gSessionStartedWallClock, observationEnd);
    } else {
        windowsEvents.channels.push_back({"event_log_System", "disabled_by_gizmo_mode", 0, true, "Gizmo compatibility mode"});
        windowsEvents.channels.push_back({"event_log_Application", "disabled_by_gizmo_mode", 0, true, "Gizmo compatibility mode"});
    }
    const bool eventReportWritten = writeWindowsEventReport(
        windowsEvents, stutterCorrelation, gPresentMonStartedWallClock,
        gSessionStartedWallClock, observationEnd, clockStable, gGameExitedUnexpectedly,
        sessionPath / L"windows_events.json");
    logEvent(eventReportWritten ? L"Windows Event Log report saved (optional evidence)" : L"Cannot save Windows Event Log report");

    const auto infrastructureEvidence =
        analyzeInfrastructureEvents(windowsEvents);
    std::wstring infrastructureEvidenceError;
    const bool infrastructureEvidenceWritten =
        writeInfrastructureEvidenceReport(
            infrastructureEvidence,
            sessionPath / L"infrastructure_evidence.json",
            sessionPath / L"infrastructure_evidence.txt",
            &infrastructureEvidenceError);

    if (infrastructureEvidenceWritten) {
        logEvent(
            infrastructureEvidence.instabilityObserved
                ? L"Infrastructure evidence  •  EXTERNAL INSTABILITY OBSERVED"
                : L"Infrastructure evidence  •  no multi-source fault pattern");
    } else {
        logEvent(
            L"Cannot save infrastructure evidence report  •  " +
            infrastructureEvidenceError);
    }

    for (const auto& source : windowsEvents.channels) {
        for (auto& observed : gEnvironmentCapabilities.sources)
            if (observed.source == source.source) observed = source;
    }
    if (!saveEnvironmentCapabilities(gEnvironmentCapabilities, sessionPath / L"environment_capabilities.json"))
        logEvent(L"Cannot update environment capabilities profile");

    auto sessionDiagnostics = analyzeSessionDiagnostics(
        summary,
        validation,
        performanceHealth,
        stutterCorrelation);

    if (!gGizmoMode && (!bodycamManifestWritten || (bodycamLogs.source.status != "available" && bodycamLogs.source.status != "available_empty")))
        sessionDiagnostics.diagnosticLimitations.push_back(L"Optional Bodycam logs: " + std::wstring(bodycamLogs.source.status.begin(), bodycamLogs.source.status.end()) + (bodycamManifestWritten ? L"" : L"; manifest write failed"));
    if (gGizmoMode)
        sessionDiagnostics.diagnosticLimitations.push_back(L"Режим Gizmo: Windows Event Log, логи Bodycam и полноэкранный помощник выключены");
    if (stutterCorrelation.videoBookmarkAlignmentAvailable)
        sessionDiagnostics.diagnosticLimitations.push_back(L"Video stutter bookmarks are navigation estimates; PresentMon first-frame and FFmpeg startup latency are not measured directly");
    else
        sessionDiagnostics.diagnosticLimitations.push_back(L"Video stutter bookmarks unavailable because stable clock and uninterrupted recorder timeline were not both confirmed");
    if (!gGizmoMode) sessionDiagnostics.diagnosticLimitations.push_back(L"Windows Event Log temporal matches are approximate and do not prove causation; see windows_events.json");
    if (infrastructureEvidence.instabilityObserved)
        sessionDiagnostics.diagnosticLimitations.push_back(L"External network/storage-path instability evidence observed; performance comparison may be contaminated; see infrastructure_evidence.txt");
    if (!infrastructureEvidenceWritten)
        sessionDiagnostics.diagnosticLimitations.push_back(L"Optional infrastructure evidence report could not be written");
    if (!eventReportWritten)
        sessionDiagnostics.diagnosticLimitations.push_back(L"Optional Windows Event Log report could not be written");
    for (const auto& channel : windowsEvents.channels) {
        if (channel.status != "available" && channel.status != "available_empty" && channel.status != "disabled_by_gizmo_mode")
            sessionDiagnostics.diagnosticLimitations.push_back(std::wstring(channel.source.begin(), channel.source.end()) + L": " + std::wstring(channel.status.begin(), channel.status.end()));
    }
    if (!clockStable)
        sessionDiagnostics.diagnosticLimitations.push_back(L"Wall clock discontinuity detected; Event Log correlation disabled");

    std::wstring sessionDiagnosticsError;
    if (writeSessionDiagnosticsReport(
            sessionDiagnostics,
            sessionPath / L"session_diagnostics.json",
            sessionPath / L"session_diagnostics.txt",
            &sessionDiagnosticsError)) {
        logEvent(
            L"Session diagnostics  •  " +
            std::to_wstring(sessionDiagnostics.correlatedStutters) +
            L" stutter(s)  •  " +
            std::to_wstring(sessionDiagnostics.signalCounts.size()) +
            L" signal type(s)");
    } else {
        logEvent(
            L"Ошибка session diagnostics report  •  " +
            sessionDiagnosticsError);
    }

    gSession->stop();
    gSession.reset();

    gLastSessionPath = sessionPath;
    gLastReportPath = sessionPath / L"report_bundle.zip";

    std::wstring packError;
    const bool packaged = packageSessionReport(
        sessionPath,
        gLastReportPath,
        &packError);

    if (!packaged)
        gLastReportPath.clear();

    if (gMirror) {
        std::wstring mirrorStopError;
        if (!gMirror->stop(&mirrorStopError)) {
            appendUiLog(
                L"Persistent mirror final sync FAILED  •  " +
                (mirrorStopError.empty()
                    ? std::wstring(L"unknown mirror error")
                    : mirrorStopError));
        }
        gMirror.reset();
    }

    gRunning = false;
    gGamePid = 0;
    updateControlsForRunningState();

    if (packaged) {
        setStatusVisual(L"Готово  •  report_bundle.zip создан");
        appendUiLog(L"Готово  •  report_bundle.zip создан");
    } else {
        setStatusVisual(L"Сессия сохранена  •  ZIP не создан");
        appendUiLog(L"ZIP не создан  •  " + packError);
    }
}

void toggleSession() {
    if (gRunning) stopSession();
    else startSession();
}

void toggleRecording() {
    if (!gRunning || !gSession) return;
    if (gRecorder) {
        gRecorder->stop();
        gRecorder.reset();
        gRecording.stop(std::chrono::steady_clock::now());
        gRecorderTimelineContinuous = false;
        if (gAudio) {
            std::wstring audioError;
            const bool audioFinalized = gAudio->stop(&audioError);
            if (!gPartAudioCaptured.empty())
                gPartAudioCaptured.back() = audioFinalized;
            gAudio.reset();
            if (!audioFinalized) {
                logEvent(L"Игровой звук недоступен  •  " + audioError);
                std::error_code audioEc;
                std::filesystem::remove(
                    recordingPartAudioPath(
                        gSession->sessionPath(),
                        static_cast<unsigned>(gRecording.intervals().size())),
                    audioEc);
            }
        }
        logEvent(L"Видео: запись остановлена");
        updateControlsForRunningState();
        return;
    }
    gRecording.starting();
    updateControlsForRunningState();
    const auto videoPreflight = runToolPreflight(
        resolveRuntimeTool(runtimeDir(), L"PresentMon.exe"),
        gRuntimeFfmpegExe,
        runtimeDir() / L"runtime-tools.sha256");
    if (!videoPreflight.recordingReady()) {
        gRecording.lost(std::chrono::steady_clock::now());
        logEvent(L"Видео: FFmpeg/NVENC/DDAGrab не готовы  •  " + videoPreflight.details);
        updateControlsForRunningState();
        return;
    }
    const auto partDirectory = recordingPartDirectory(
        gSession->sessionPath(),
        static_cast<unsigned>(gRecording.intervals().size() + 1));
    std::error_code partError;
    const bool previousAttemptExists = std::filesystem::exists(partDirectory, partError);
    if (!partError && previousAttemptExists) {
        const auto failedAttempt = partDirectory.parent_path() /
            (partDirectory.filename().wstring() + L"_failed_start_" + std::to_wstring(GetTickCount64()));
        std::filesystem::rename(partDirectory, failedAttempt, partError);
        if (!partError)
            logEvent(L"Предыдущая попытка записи сохранена  •  " + failedAttempt.filename().wstring());
    }
    if (partError) {
        gRecording.lost(std::chrono::steady_clock::now());
        logEvent(L"Видео: не удалось сохранить предыдущую попытку записи  •  " + std::to_wstring(partError.value()));
        updateControlsForRunningState();
        return;
    }
    gRecorder = std::make_unique<RecorderController>(gRuntimeFfmpegExe);
    std::wstring error;
    if (!gRecorder->start(partDirectory, &error)) {
        gRecorder.reset();
        gRecording.lost(std::chrono::steady_clock::now());
        logEvent(L"Видео: не удалось начать запись  •  " + error);
    } else {
        gRecording.start(std::chrono::steady_clock::now());
        gPartAudioCaptured.push_back(false);
        gRecorderStartedWallClock = std::chrono::system_clock::now();
        gRecorderStartObserved = true;
        gRecorderTimelineContinuous = gRecording.intervals().size() == 1;
        logEvent(L"Видео: запись началась");
        gAudio = std::make_unique<AudioLoopbackRecorder>();
        std::wstring audioError;
        if (!gAudio->start(
                recordingPartAudioPath(
                    gSession->sessionPath(),
                    static_cast<unsigned>(gRecording.intervals().size())),
                &audioError)) {
            gAudio.reset();
            std::error_code audioEc;
            std::filesystem::remove(partDirectory / L"game_audio.wav", audioEc);
            logEvent(L"Игровой звук недоступен  •  " + audioError);
        } else {
            logEvent(L"Игровой звук: запись началась");
        }
    }
    updateControlsForRunningState();
}

void openLastSession(HWND owner) {
    if (gLastSessionPath.empty()) return;
    ShellExecuteW(
        owner, L"open",
        gLastSessionPath.c_str(),
        nullptr, nullptr, SW_SHOWNORMAL);
}

void copyLastReportPath(HWND owner) {
    if (gLastReportPath.empty()) return;

    if (copyTextToClipboard(owner, gLastReportPath.wstring())) {
        setStatusVisual(L"Путь к report_bundle.zip скопирован");
        appendUiLog(L"Путь ZIP скопирован в буфер обмена");
    }
}

void createFonts() {
    gTitleFont = CreateFontW(
        -29, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gSubtitleFont = CreateFontW(
        -13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gBodyFont = CreateFontW(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gSmallFont = CreateFontW(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gButtonFont = CreateFontW(
        -15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void deleteFont(HFONT& font) {
    if (font) {
        DeleteObject(font);
        font = nullptr;
    }
}

void destroyFonts() {
    deleteFont(gTitleFont);
    deleteFont(gSubtitleFont);
    deleteFont(gBodyFont);
    deleteFont(gSmallFont);
    deleteFont(gButtonFont);
}

void applyDarkWindowChrome(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));

    const int rounded = 2;
    DwmSetWindowAttribute(hwnd, 33, &rounded, sizeof(rounded));
}

// Dashboard rendering uses GDI only. No browser runtime, GPU hooks or extra
// processes are needed, including in Gizmo compatibility mode.
void fillRegion(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void drawCard(HDC dc, const RECT& rect, COLORREF color = COLOR_PANEL) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, COLOR_BORDER);
    const auto oldBrush = SelectObject(dc, brush);
    const auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 18, 18);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void drawTextLine(HDC dc, const wchar_t* text, RECT rect, HFONT font,
                  COLORREF color, UINT format = DT_LEFT | DT_VCENTER |
                                                  DT_SINGLELINE | DT_END_ELLIPSIS) {
    if (!font) return;
    const auto oldFont = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text, -1, &rect, format);
    SelectObject(dc, oldFont);
}

void drawCircle(HDC dc, int x, int y, int diameter, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    const auto oldBrush = SelectObject(dc, brush);
    const auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, x, y, x + diameter, y + diameter);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(brush);
}

std::wstring compactValue(double value, int decimals = 0) {
    std::wstringstream out;
    out << std::fixed << std::setprecision(decimals) << value;
    return out.str();
}

void paintMetric(HDC dc, const RECT& area, const wchar_t* label,
                 const std::wstring& value, const wchar_t* unit) {
    drawTextLine(dc, label,
        RECT{area.left + 13, area.top + 2, area.right - 6, area.top + 24},
        gSubtitleFont, COLOR_MUTED);
    drawTextLine(dc, value.c_str(),
        RECT{area.left + 13, area.top + 29, area.right - 6, area.top + 75},
        gTitleFont, COLOR_TEXT);
    drawTextLine(dc, unit,
        RECT{area.left + 13, area.top + 75, area.right - 6, area.top + 98},
        gSmallFont, COLOR_MUTED);
}

void paintDashboard(HDC dc, int width, int height) {
    fillRegion(dc, RECT{0, 0, width, height}, COLOR_BG);
    const int left = 24;
    const int right = width - 24;
    const int span = right - left;

    // Thin separators, not separate boxed panels.
    fillRegion(dc, RECT{left, 62, right, 63}, COLOR_BORDER);
    drawTextLine(dc, L"ПРЕСЕТ",
        RECT{left + 4, kConfigTop, 102, kConfigTop + 35},
        gSubtitleFont, COLOR_MUTED);
    fillRegion(dc, RECT{left, 122, right, 123}, COLOR_BORDER);
    fillRegion(dc, RECT{left, 252, right, 253}, COLOR_BORDER);
    fillRegion(dc, RECT{left, 365, right, 366}, COLOR_BORDER);
    fillRegion(dc, RECT{left, height - kFooterHeight, right,
                         height - kFooterHeight + 1}, COLOR_BORDER);

    const int col = span / 3;
    const int testX = left;
    const int videoX = left + col;
    const int statusX = left + col * 2;
    fillRegion(dc, RECT{videoX - 8, kActionsTop, videoX - 7, 239}, COLOR_BORDER);
    fillRegion(dc, RECT{statusX - 8, kActionsTop, statusX - 7, 239}, COLOR_BORDER);

    drawTextLine(dc, L"ТЕСТ", RECT{testX + 4, kActionsTop - 4,
                                 testX + 240, kActionsTop + 20},
                 gSubtitleFont, COLOR_TEXT);
    drawTextLine(dc, L"ЗАПИСЬ", RECT{videoX + 4, kActionsTop - 4,
                                   videoX + 240, kActionsTop + 20},
                 gSubtitleFont, COLOR_TEXT);
    drawTextLine(dc, L"СТАТУС", RECT{statusX + 4, kActionsTop - 4,
                                   right, kActionsTop + 20},
                 gSubtitleFont, COLOR_TEXT);

    drawTextLine(dc, L"PresentMon · FPS · микрофризы",
        RECT{testX + 210, kActionsTop + 36, videoX - 16, kActionsTop + 93},
        gSmallFont, COLOR_MUTED);
    drawTextLine(dc, L"FFmpeg / NVIDIA NVENC",
        RECT{videoX + 210, kActionsTop + 36, statusX - 16, kActionsTop + 93},
        gSmallFont, COLOR_MUTED);
    drawCircle(dc, statusX + 8, kActionsTop + 39, 10,
        gRunning ? COLOR_OK : COLOR_MUTED);

    const int metricWidth = span / 4;
    for (int i = 1; i < 4; ++i) {
        const int x = left + metricWidth * i;
        fillRegion(dc, RECT{x - 2, kMetricsTop, x - 1, 350}, COLOR_BORDER);
    }
    const auto metricArea = [&](int i) {
        return RECT{left + i * metricWidth, kMetricsTop,
                    i == 3 ? right : left + (i + 1) * metricWidth, 352};
    };
    paintMetric(dc, metricArea(0), L"FPS",
        gDashboardFrame.valid ? compactValue(gDashboardFrame.fps) : L"—",
        L"Частота кадров");
    paintMetric(dc, metricArea(1), L"FRAMETIME",
        gDashboardFrame.valid
            ? compactValue(gDashboardFrame.frameTimeMs, 1) + L" ms" : L"—",
        L"Время кадра");
    paintMetric(dc, metricArea(2), L"1% LOW",
        gDashboardFrame.valid
            ? compactValue(gDashboardFrame.onePercentLowFps) : L"—",
        L"Плавность");
    paintMetric(dc, metricArea(3), L"GPU",
        gDashboardTelemetry.gpuValid
            ? std::to_wstring(gDashboardTelemetry.gpuUtilPct) + L" %" : L"—",
        L"Загрузка видеокарты");

    drawTextLine(dc, L"ЖУРНАЛ СОБЫТИЙ",
        RECT{left + 5, kLogTop + 2, right - 140, kLogTop + 38},
        gSubtitleFont, COLOR_TEXT);
    drawTextLine(dc, L"ПОСЛЕДНИЙ ОТЧЁТ",
        RECT{left + 5, height - kFooterHeight + 5,
             left + 210, height - kFooterHeight + 36},
        gSubtitleFont, COLOR_MUTED);

    const wchar_t* lastReport = gLastReportPath.empty()
        ? L"Отчёт пока не сформирован"
        : L"Результаты сохранены. Открыть папку или скопировать путь ZIP.";
    drawTextLine(dc, lastReport,
        RECT{left + 180, height - kFooterHeight + 5,
             right - 395, height - kFooterHeight + 37},
        gSmallFont, COLOR_MUTED);
}

void paintMainWindow(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC screen = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) {
        EndPaint(hwnd, &ps);
        return;
    }

    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = memory ? CreateCompatibleBitmap(screen, width, height) : nullptr;
    if (!memory || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        FillRect(screen, &client, gBackgroundBrush);
        EndPaint(hwnd, &ps);
        return;
    }
    const auto oldBitmap = SelectObject(memory, bitmap);
    paintDashboard(memory, width, height);
    BitBlt(screen, 0, 0, width, height, memory, 0, 0, SRCCOPY);
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    EndPaint(hwnd, &ps);
}

void drawButton(const DRAWITEMSTRUCT* dis) {
    if (!dis || dis->CtlType != ODT_BUTTON) return;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool nav = dis->hwndItem == gNavHome ||
        dis->hwndItem == gNavPreset ||
        dis->hwndItem == gNavRecord ||
        dis->hwndItem == gNavReports ||
        dis->hwndItem == gNavSettings ||
        dis->hwndItem == gHelp;
    const bool test = dis->hwndItem == gToggle;
    const bool video = dis->hwndItem == gRecord;
    const bool activeVideo = gRecorder && gRecorder->running();

    COLORREF fill = nav ? COLOR_BG : COLOR_PANEL;
    COLORREF text = disabled ? COLOR_MUTED : COLOR_TEXT;
    COLORREF outline = nav ? COLOR_BG : COLOR_BORDER;

    if (test && gRunning && !disabled) {
        fill = RGB(31, 74, 51);
        outline = COLOR_OK;
        text = COLOR_OK;
    } else if (video && activeVideo && !disabled) {
        fill = RGB(91, 32, 40);
        outline = COLOR_STOP;
        text = RGB(253, 214, 219);
    } else if (video && gRecording.state() == RecordingState::Lost && !disabled) {
        outline = COLOR_STOP;
        text = COLOR_STOP;
    }
    if (pressed && !disabled) fill = COLOR_PANEL_HOVER;
    if (disabled) fill = COLOR_DISABLED;
    if (nav && !disabled) fill = COLOR_BG;

    RECT r = dis->rcItem;
    if (!nav) {
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, outline);
        const auto oldBrush = SelectObject(dis->hDC, brush);
        const auto oldPen = SelectObject(dis->hDC, pen);
        RoundRect(dis->hDC, r.left, r.top, r.right, r.bottom, 9, 9);
        SelectObject(dis->hDC, oldPen);
        SelectObject(dis->hDC, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
    } else {
        // Owner-drawn nav buttons must paint their own background. Otherwise
        // focus/hover redraws can leave stale characters on the button DC.
        fillRegion(dis->hDC, r, COLOR_BG);
        if (!disabled && (pressed || (dis->itemState & ODS_FOCUS))) {
            fillRegion(dis->hDC,
                RECT{r.left + 10, r.bottom - 3, r.right - 10, r.bottom - 1},
                COLOR_MUTED);
        }
    }

    wchar_t caption[160]{};
    GetWindowTextW(dis->hwndItem, caption,
                   static_cast<int>(std::size(caption)));
    const auto oldFont = SelectObject(dis->hDC,
        nav ? gSmallFont : gButtonFont);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, text);
    DrawTextW(dis->hDC, caption, -1, &r,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dis->hDC, oldFont);
}

void drawPresetItem(const DRAWITEMSTRUCT* dis) {
    if (!dis || dis->CtlType != ODT_COMBOBOX)
        return;

    HDC dc = dis->hDC;
    RECT r = dis->rcItem;

    const bool selected = (dis->itemState & ODS_SELECTED) != 0;
    HBRUSH background = CreateSolidBrush(
        selected ? RGB(33, 40, 53) : COLOR_PANEL);

    FillRect(dc, &r, background);
    DeleteObject(background);

    UINT itemId = dis->itemID;
    if (itemId == static_cast<UINT>(-1)) {
        const LRESULT current = SendMessageW(dis->hwndItem, CB_GETCURSEL, 0, 0);
        if (current != CB_ERR)
            itemId = static_cast<UINT>(current);
    }

    if (itemId != static_cast<UINT>(-1)) {
        wchar_t text[192]{};
        SendMessageW(
            dis->hwndItem,
            CB_GETLBTEXT,
            itemId,
            reinterpret_cast<LPARAM>(text));

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, COLOR_TEXT);
        SelectObject(dc, gBodyFont);

        RECT textRect = r;
        textRect.left += 10;
        textRect.right -= 8;

        DrawTextW(
            dc,
            text,
            -1,
            &textRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

void layoutControls(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = client.right;
    const int height = client.bottom;
    if (width < 500 || height < 500) return;
    const int left = 24;
    const int right = width - 24;
    const int span = right - left;

    // Functional top shortcuts, not decorative tabs to nonexistent pages.
    const HWND navs[] = {
        gNavHome, gNavPreset, gNavRecord,
        gNavReports, gNavSettings, gHelp
    };
    const int navWidth = span / 6;
    for (int i = 0; i < 6; ++i) {
        if (navs[i]) MoveWindow(navs[i], left + i * navWidth, kNavTop,
                                i == 5 ? span - 5 * navWidth : navWidth,
                                kNavHeight, TRUE);
    }

    // The full preset name stays intact for session provenance.
    if (gPreset) MoveWindow(gPreset, 110, kConfigTop, 
                            (std::max)(300, width - 490), 240, TRUE);
    if (gGizmo) MoveWindow(gGizmo, right - 280, kConfigTop,
                           266, 37, TRUE);

    const int col = span / 3;
    if (gToggle) MoveWindow(gToggle, left + 4, kActionsTop + 31, 193, 52, TRUE);
    if (gRecord) MoveWindow(gRecord, left + col + 4,
                            kActionsTop + 31, 193, 52, TRUE);
    if (gStatus) MoveWindow(gStatus, left + col * 2 + 25,
                            kActionsTop + 29, col - 53, 32, TRUE);
    if (gVideoStatus) MoveWindow(gVideoStatus, left + col + 4,
                                 kActionsTop + 83, col - 35, 27, TRUE);

    const int footer = height - kFooterHeight;
    if (gClearLog) MoveWindow(gClearLog, right - 118, kLogTop + 2,
                              118, 30, TRUE);
    if (gLog) MoveWindow(gLog, left + 5, kLogTop + 39, span - 10,
                         (std::max)(90, footer - kLogTop - 49), TRUE);

    if (gOpenSession) MoveWindow(gOpenSession, right - 365, footer + 11,
                                 173, 39, TRUE);
    if (gCopyReport) MoveWindow(gCopyReport, right - 183, footer + 11,
                                183, 39, TRUE);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void createControls(HWND hwnd) {
    const auto nav = [&](int id, const wchar_t* label) {
        HWND button = CreateWindowW(
            L"BUTTON", label, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 140, kNavHeight, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
        if (button) SendMessageW(button, WM_SETFONT,
                                reinterpret_cast<WPARAM>(gSmallFont), TRUE);
        return button;
    };
    gNavHome = nav(ID_NAV_HOME, L"ГЛАВНАЯ");
    gNavPreset = nav(ID_NAV_PRESET, L"DLSS ПРЕСЕТ");
    gNavRecord = nav(ID_NAV_RECORD, L"ЗАПИСЬ И ВИДЕО");
    gNavReports = nav(ID_NAV_REPORTS, L"ТЕСТЫ И ОТЧЁТЫ");
    gNavSettings = nav(ID_NAV_SETTINGS, L"НАСТРОЙКИ");
    gHelp = nav(ID_HELP, L"СПРАВКА");

    gPreset = CreateWindowExW(
        0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
        0, 0, 340, 240,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PRESET)), nullptr, nullptr);
    SendMessageW(gPreset, WM_SETFONT, reinterpret_cast<WPARAM>(gBodyFont), TRUE);
    SendMessageW(gPreset, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), 30);
    SendMessageW(gPreset, CB_SETITEMHEIGHT, 0, 30);
    SetWindowTheme(gPreset, L"DarkMode_Explorer", nullptr);
    SendMessageW(gPreset, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(L"DLSS Quality + Frame Generation OFF"));
    SendMessageW(gPreset, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(L"DLSS Quality + AMD FSR Frame Generation"));
    SendMessageW(gPreset, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(L"DLSS Quality + NVIDIA DLSS Frame Generation 2x"));
    SendMessageW(gPreset, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(L"DLSS Quality + Smooth Motion ON"));
    SendMessageW(gPreset, CB_SETCURSEL, 0, 0);

    gGizmo = CreateWindowW(L"BUTTON", L"Gizmo: режим совместимости",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 266, 37, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_GIZMO)), nullptr, nullptr);
    SendMessageW(gGizmo, WM_SETFONT, reinterpret_cast<WPARAM>(gSmallFont), TRUE);
    SetWindowTheme(gGizmo, L"DarkMode_Explorer", nullptr);

    gStatus = CreateWindowW(
        L"STATIC", L"Ожидание Bodycam...",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 220, 32, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)), nullptr, nullptr);
    SendMessageW(gStatus, WM_SETFONT, reinterpret_cast<WPARAM>(gBodyFont), TRUE);
    gVideoStatus = CreateWindowW(
        L"STATIC", L"Видео выключено",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 215, 28, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_VIDEO_STATUS)), nullptr, nullptr);
    SendMessageW(gVideoStatus, WM_SETFONT, reinterpret_cast<WPARAM>(gSmallFont), TRUE);

    gToggle = CreateWindowW(
        L"BUTTON", L"▶  F9  НАЧАТЬ ТЕСТ",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 193, 52, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TOGGLE)), nullptr, nullptr);
    gRecord = CreateWindowW(
        L"BUTTON", L"●  F8  ЗАПИСЬ ВИДЕО",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 193, 52, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_RECORD)), nullptr, nullptr);
    gOpenSession = CreateWindowW(
        L"BUTTON", L"ОТКРЫТЬ ПАПКУ",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 173, 39, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_OPEN_SESSION)), nullptr, nullptr);
    gCopyReport = CreateWindowW(
        L"BUTTON", L"КОПИРОВАТЬ ZIP",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 183, 39, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_COPY_REPORT)), nullptr, nullptr);
    gClearLog = CreateWindowW(
        L"BUTTON", L"Очистить окно",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 118, 30, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CLEAR_LOG)), nullptr, nullptr);

    gLog = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        0, 0, 300, 250, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LOG)), nullptr, nullptr);
    SendMessageW(gLog, WM_SETFONT, reinterpret_cast<WPARAM>(gSmallFont), TRUE);
    SendMessageW(gLog, EM_LIMITTEXT, 0, 0);
    SetWindowTheme(gLog, L"DarkMode_Explorer", nullptr);

    EnableWindow(gRecord, FALSE);
    EnableWindow(gOpenSession, FALSE);
    EnableWindow(gCopyReport, FALSE);
    layoutControls(hwnd);
}

void updateLiveOverlay() {
    if (!gRunning)
        return;

    const auto bodycam = findProcessByName(L"Bodycam-Win64-Shipping.exe");
    if (!bodycam.pid || bodycam.pid != gGamePid) {
        gGameExitedUnexpectedly = true;
        logEvent(
            L"Bodycam завершился во время теста  •  возможный crash/exit");
        stopSession();
        return;
    }

    if (gMirror) {
        std::wstring mirrorEvent;
        if (!gMirror->healthy(&mirrorEvent)) {
            logEvent(
                L"Persistent mirror потерян во время теста  •  " +
                (mirrorEvent.empty()
                    ? std::wstring(L"runtime failure")
                    : mirrorEvent));

            gEnvironmentCapabilities.sources.push_back({
                "persistent_mirror_runtime",
                "failed_during_capture",
                0,
                false,
                "Configured crash-survival mirror failed after startup"
            });

            stopSession();
            return;
        }
    }

    if (gPresentMon) {
        std::wstring presentMonEvent;
        if (!gPresentMon->healthy(&presentMonEvent)) {
            logEvent(
                L"Обязательный источник потерян  •  " +
                (presentMonEvent.empty()
                    ? std::wstring(L"PresentMon unavailable")
                    : presentMonEvent));

            gEnvironmentCapabilities.sources.push_back({
                "presentmon_runtime",
                "failed_during_capture",
                0,
                false,
                "Mandatory PresentMon source exited after startup"
            });

            stopSession();
            return;
        }
    }

    if (gAudio) {
        std::wstring audioEvent;
        if (!gAudio->healthy(&audioEvent)) {
            logEvent(
                L"Optional game audio потерян  •  " +
                (audioEvent.empty()
                    ? std::wstring(L"WASAPI stopped unexpectedly")
                    : audioEvent));

            gEnvironmentCapabilities.sources.push_back({
                "wasapi_audio_runtime",
                "failed_during_capture",
                0,
                true,
                "Optional WASAPI loopback failed after startup; partial WAV discarded"
            });

            gAudio->stop();
            gAudio.reset();

            std::error_code audioEc;
            std::filesystem::remove(
                recordingPartAudioPath(
                    gSession->sessionPath(),
                    static_cast<unsigned>(gRecording.intervals().size())),
                audioEc);
        }
    }

    if (gRecorder) {
        std::wstring recorderEvent;
        const bool recorderHealthy =
            gRecorder->maintain(&recorderEvent);

        if (!recorderEvent.empty()) {
            gRecorderTimelineContinuous = false;
            logEvent(recorderEvent);
        }

        if (!recorderHealthy) {
            if (recorderEvent.empty()) logEvent(L"Видео: захват потерян");

            gRecorderTimelineContinuous = false;
            gEnvironmentCapabilities.sources.push_back({
                "nvenc_video_runtime",
                "failed_during_capture",
                0,
                true,
                "Optional FFmpeg recorder stopped; diagnostic capture continues"
            });
            gRecording.lost(std::chrono::steady_clock::now());
            gRecorder->stop();
            gRecorder.reset();
            if (gAudio) {
                std::wstring audioError;
                const bool audioFinalized = gAudio->stop(&audioError);
                if (!gPartAudioCaptured.empty())
                    gPartAudioCaptured.back() = audioFinalized;
                gAudio.reset();
                if (!audioFinalized) {
                    logEvent(L"Игровой звук недоступен  •  " + audioError);
                    std::error_code audioEc;
                    std::filesystem::remove(
                        recordingPartAudioPath(
                            gSession->sessionPath(),
                            static_cast<unsigned>(gRecording.intervals().size())),
                        audioEc);
                }
            }
            updateControlsForRunningState();
        }
    }

    gLiveFrames.poll();

    const LiveFrameSnapshot frame = gLiveFrames.snapshot();
    const TelemetrySnapshot telemetry =
        gTelemetry ? gTelemetry->latest() : TelemetrySnapshot{};

    gDashboardFrame = frame;
    gDashboardTelemetry = telemetry;
    if (gMain) InvalidateRect(gMain, nullptr, FALSE);

    if (gFullscreenHudActive) {
        gFullscreenOverlay.publish(gGamePid, frame, telemetry);
    } else {
        gOverlay.update(frame, telemetry);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        gMain = hwnd;
        createFonts();

        gBackgroundBrush = CreateSolidBrush(COLOR_BG);
        gPanelBrush = CreateSolidBrush(COLOR_PANEL);

        applyDarkWindowChrome(hwnd);
        createControls(hwnd);

        gOverlay.create(
            reinterpret_cast<LPCREATESTRUCTW>(lp)->hInstance);

        RegisterHotKey(hwnd, HOTKEY_TOGGLE, MOD_NOREPEAT, VK_F9);
        RegisterHotKey(hwnd, HOTKEY_RECORD, MOD_NOREPEAT, VK_F8);
        SetTimer(hwnd, ID_TIMER, 500, nullptr);

        appendUiLog(L"Приложение готово");
        appendUiLog(L"Каталог сессий и отчётов  •  " + gSessionRoot.wstring());
#ifdef BODYCAM_SINGLE_EXE
        appendUiLog(L"Встроенные инструменты распакованы и проверены  •  " + gRuntimeRoot.wstring());
        appendUiLog(L"HUD: оконный/Borderless; UIAccess helper не включён");
#endif
        return 0;

    case WM_GETMINMAXINFO: {
        auto* size = reinterpret_cast<MINMAXINFO*>(lp);
        size->ptMinTrackSize = POINT{1040, 740};
        return 0;
    }

    case WM_SIZE:
        if (gPreset) layoutControls(hwnd);
        return 0;

    case WM_ERASEBKGND:
        // The dashboard is drawn into one off-screen bitmap during WM_PAINT.
        return 1;

    case WM_PAINT:
        paintMainWindow(hwnd);
        return 0;

    case WM_DRAWITEM: {
        const auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (dis->CtlType == ODT_BUTTON) {
            drawButton(dis);
            return TRUE;
        }
        if (dis->CtlType == ODT_COMBOBOX) {
            drawPresetItem(dis);
            return TRUE;
        }
        break;
    }

    case WM_MEASUREITEM: {
        auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        if (mis && mis->CtlType == ODT_COMBOBOX && mis->CtlID == ID_PRESET) {
            mis->itemHeight = 28;
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wp);
        HWND control = reinterpret_cast<HWND>(lp);

        SetBkMode(dc, OPAQUE);
        if (control == gStatus || control == gVideoStatus) {
            const bool videoFailed =
                control == gVideoStatus &&
                gRecording.state() == RecordingState::Lost;
            SetTextColor(dc, videoFailed ? COLOR_STOP : COLOR_TEXT);
            SetBkColor(dc, COLOR_BG);
            return reinterpret_cast<INT_PTR>(gBackgroundBrush);
        }

        if (control == gLog) {
            SetTextColor(dc, RGB(190, 200, 214));
            SetBkColor(dc, COLOR_PANEL);
            return reinterpret_cast<INT_PTR>(gPanelBrush);
        }

        SetTextColor(dc, COLOR_TEXT);
        SetBkColor(dc, COLOR_BG);
        return reinterpret_cast<INT_PTR>(gBackgroundBrush);
    }

    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, COLOR_TEXT);
        SetBkColor(dc, COLOR_BG);
        return reinterpret_cast<INT_PTR>(gBackgroundBrush);
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, RGB(190, 200, 214));
        SetBkColor(dc, COLOR_PANEL);
        return reinterpret_cast<INT_PTR>(gPanelBrush);
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_NAV_HOME:
            if (gToggle) SetFocus(gToggle);
            return 0;
        case ID_NAV_PRESET:
            if (gPreset && IsWindowEnabled(gPreset)) {
                SetFocus(gPreset);
                SendMessageW(gPreset, CB_SHOWDROPDOWN, TRUE, 0);
            }
            return 0;
        case ID_NAV_RECORD:
            if (gRecord && IsWindowEnabled(gRecord))
                SetFocus(gRecord);
            return 0;
        case ID_NAV_REPORTS:
            if (gLog) SetFocus(gLog);
            return 0;
        case ID_NAV_SETTINGS:
#ifdef BODYCAM_EXPERIMENTAL_AUTOTUNE
            if (!gRunning) {
                HMENU menu = CreatePopupMenu();
                if (menu) {
                    AppendMenuW(menu, MF_STRING, ID_AUTOTUNE_RUN,
                        L"Автоподбор графики (эксперимент)");
                    AppendMenuW(menu, MF_STRING, ID_AUTOTUNE_RESTORE,
                        L"Восстановить исходные настройки Bodycam");
                    RECT rect{};
                    GetWindowRect(gNavSettings, &rect);
                    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN,
                        rect.left, rect.bottom, 0, hwnd, nullptr);
                    DestroyMenu(menu);
                }
            }
#else
            if (gGizmo && IsWindowEnabled(gGizmo))
                SetFocus(gGizmo);
#endif
            return 0;
#ifdef BODYCAM_EXPERIMENTAL_AUTOTUNE
        case ID_AUTOTUNE_RUN:
        case ID_AUTOTUNE_RESTORE:
            runGameAutoTune(hwnd, gSessionRoot.parent_path(),
                LOWORD(wp) == ID_AUTOTUNE_RESTORE,
                gGizmo && SendMessageW(gGizmo, BM_GETCHECK, 0, 0) == BST_CHECKED,
                gRunning);
            return 0;
#endif
        case ID_CLEAR_LOG:
            // Only the visible window is cleared. Session logs on disk and
            // mirrored diagnostic evidence remain untouched.
            if (gLog) SetWindowTextW(gLog, L"");
            return 0;
        case ID_PRESET:
            if (HIWORD(wp) == CBN_SELCHANGE)
                InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case ID_TOGGLE:
            toggleSession();
            return 0;

        case ID_RECORD:
            toggleRecording();
            return 0;

        case ID_HELP: {
#ifdef BODYCAM_SINGLE_EXE
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, ID_HELP_TEXT, L"Справка");
            AppendMenuW(menu, MF_STRING, ID_LICENSES, L"Лицензии и исходники компонентов");
            POINT point{}; GetCursorPos(&point);
            TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN, point.x, point.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            return 0;
        }
        case ID_HELP_TEXT:
        case ID_LICENSES: {
            const auto help = runtimeDir() / (LOWORD(wp) == ID_LICENSES ? L"licenses.txt" : L"help.txt");
#else
            const auto help = appDir() / L"СПРАВКА.txt";
#endif
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd, L"open", help.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
                MessageBoxW(hwnd, (L"Не удалось открыть справку:\n" + help.wstring()).c_str(), L"Справка", MB_ICONWARNING);
            return 0;
        }

        case ID_OPEN_SESSION:
            openLastSession(hwnd);
            return 0;

        case ID_COPY_REPORT:
            copyLastReportPath(hwnd);
            return 0;

        default:
            break;
        }
        break;

    case WM_TIMER:
        if (wp == ID_TIMER) {
            if (gRunning) {
                updateLiveOverlay();
            } else {
                if (++gIdleTick >= 3) {
                    gIdleTick = 0;
                    refreshGameState();
                }
            }
            return 0;
        }
        break;

    case WM_HOTKEY:
        if (wp == HOTKEY_RECORD) {
            toggleRecording();
            return 0;
        }
        if (wp == HOTKEY_TOGGLE) {
            if (gSession)
                gSession->log(L"Hotkey received: F9 toggle");
            toggleSession();
            return 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER);
        UnregisterHotKey(hwnd, HOTKEY_TOGGLE);
        UnregisterHotKey(hwnd, HOTKEY_RECORD);

        if (gRunning)
            stopSession();

        gFullscreenOverlay.shutdown();
        gOverlay.destroy();

        if (gBackgroundBrush) {
            DeleteObject(gBackgroundBrush);
            gBackgroundBrush = nullptr;
        }
        if (gPanelBrush) {
            DeleteObject(gPanelBrush);
            gPanelBrush = nullptr;
        }
        destroyFonts();
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    std::wstring storageError;
#ifdef BODYCAM_SINGLE_EXE
    if (!prepareEmbeddedRuntime(gRuntimeRoot, &storageError)) {
        MessageBoxW(nullptr, storageError.c_str(), L"Bodycam Diagnostics — встроенные инструменты", MB_ICONERROR);
        return 1;
    }
    constexpr bool perUserOnly = true;
#else
    constexpr bool perUserOnly = false;
#endif
    gSessionRoot = writableSessionRoot(appDir(), perUserOnly, &storageError);
    if (gSessionRoot.empty()) {
        MessageBoxW(nullptr, storageError.c_str(), L"Bodycam Diagnostics — каталог данных", MB_ICONERROR);
        return 1;
    }

    INITCOMMONCONTROLSEX icc{
        sizeof(icc),
        ICC_STANDARD_CLASSES
    };
    InitCommonControlsEx(&icc);

    const wchar_t* className = L"BodycamDiagnosticsWindow";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;

    if (!RegisterClassW(&wc) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0,
        className,
        L"Bodycam Diagnostics  |  Performance Lab",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1190, 930,
        nullptr, nullptr,
        hInstance, nullptr);

    if (!hwnd)
        return 1;

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
