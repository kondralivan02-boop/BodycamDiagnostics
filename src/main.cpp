#include <Windows.h>
#include <CommCtrl.h>
#include <Shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

#include "AudioLoopbackRecorder.h"
#include "AudioVideoMuxer.h"
#include "CrashCollector.h"
#include "CaptureStartupPolicy.h"
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
constexpr int ID_TIMER = 1;
constexpr int HOTKEY_TOGGLE = 2001;

constexpr COLORREF COLOR_BG = RGB(13, 16, 22);
constexpr COLORREF COLOR_PANEL = RGB(22, 27, 36);
constexpr COLORREF COLOR_PANEL_HOVER = RGB(29, 35, 46);
constexpr COLORREF COLOR_BORDER = RGB(48, 57, 72);
constexpr COLORREF COLOR_TEXT = RGB(235, 239, 246);
constexpr COLORREF COLOR_MUTED = RGB(143, 154, 173);
constexpr COLORREF COLOR_ACCENT = RGB(78, 139, 255);
constexpr COLORREF COLOR_ACCENT_PRESSED = RGB(62, 118, 225);
constexpr COLORREF COLOR_STOP = RGB(168, 72, 82);
constexpr COLORREF COLOR_DISABLED = RGB(53, 59, 70);
constexpr COLORREF COLOR_OK = RGB(76, 207, 133);

HWND gMain{};
HWND gPreset{};
HWND gToggle{};
HWND gOpenSession{};
HWND gCopyReport{};
HWND gStatus{};
HWND gLog{};

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

DiagnosticsOverlay gOverlay;
FullscreenOverlayBridge gFullscreenOverlay;
LiveFrameMetrics gLiveFrames;
bool gFullscreenHudActive = false;

std::filesystem::path gLastSessionPath;
std::filesystem::path gLastReportPath;
std::filesystem::path gRuntimePresentMonExe;
std::filesystem::path gRuntimeFfmpegExe;

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

    SetWindowTextW(gToggle, gRunning ? L"Остановить тест" : L"Начать тест");
    EnableWindow(gPreset, gRunning ? FALSE : TRUE);
    EnableWindow(gOpenSession, !gRunning && !gLastSessionPath.empty());
    EnableWindow(gCopyReport, !gRunning && !gLastReportPath.empty());
    InvalidateRect(gToggle, nullptr, TRUE);
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

    const auto runtimeRoot = appDir();
    const auto presentMonExe =
        resolveRuntimeTool(runtimeRoot, L"PresentMon.exe");
    const auto ffmpegExe =
        resolveRuntimeTool(runtimeRoot, L"ffmpeg.exe");

    gRuntimePresentMonExe = presentMonExe;
    gRuntimeFfmpegExe = ffmpegExe;

    const auto preflight = runToolPreflight(
        presentMonExe,
        ffmpegExe,
        runtimeRoot / L"runtime-tools.sha256");

    if (!preflight.hashManifestTrusted ||
        !preflight.presentMonFound ||
        !preflight.presentMonTrusted ||
        !preflight.recordingReady()) {
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

    gSession = std::make_unique<SessionManager>(appDir() / L"Sessions");
    if (!gSession->start(presetName)) {
        MessageBoxW(
            gMain,
            L"Не удалось создать папку сессии.",
            L"Bodycam Diagnostics",
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
    gLastSessionPath.clear();
    gLastReportPath.clear();

    writeSystemSnapshot(gSession->sessionPath() / L"system.json");
    logEvent(L"Сессия создана  •  " + presetName);
    logEvent(L"Bodycam PID  •  " + std::to_wstring(bodycam.pid));

    gEnvironmentCapabilities = probeEnvironmentCapabilities(bodycam.pid);
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

    gRecorder = std::make_unique<RecorderController>(
        ffmpegExe);

    std::wstring recorderError;
    const bool recorderStarted =
        gRecorder->start(
            gSession->sessionPath() / L"video",
            &recorderError);

    if (recorderStarted) {
        gRecorderStartedWallClock = std::chrono::system_clock::now();
        gRecorderStartObserved = true;
        gRecorderTimelineContinuous = true;
        gEnvironmentCapabilities.sources.push_back({"nvenc_video", "started", 0, false, "Startup succeeded; completeness validated separately"});
        logEvent(L"NVENC video  •  ON");
    } else {
        gEnvironmentCapabilities.sources.push_back({"nvenc_video", "unavailable", 0, false, "See session.log for startup error; no Win32 code supplied by controller"});
        logEvent(L"NVENC video недоступно  •  " + recorderError);
    }

    if (!requiredCaptureSourcesReady(
            presentMonStarted,
            recorderStarted,
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

        if (!recorderStarted) {
            if (!failure.empty())
                failure += L"\n";
            failure += L"NVENC video: ";
            failure +=
                recorderError.empty()
                    ? L"startup failed"
                    : recorderError;
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

    gAudio = std::make_unique<AudioLoopbackRecorder>();
    std::wstring audioError;
    if (gAudio->start(
            gSession->sessionPath() / L"game_audio.wav",
            &audioError)) {
        gEnvironmentCapabilities.sources.push_back({"wasapi_audio", "started", 0, true, "Startup succeeded; completeness validated separately"});
        logEvent(L"Game audio  •  ON");
    } else {
        gEnvironmentCapabilities.sources.push_back({"wasapi_audio", "unavailable", 0, true, "See session.log for startup error; no Win32 code supplied by controller"});
        logEvent(L"Game audio недоступен  •  " + audioError);
        gAudio.reset();
    }

    if (!saveEnvironmentCapabilities(gEnvironmentCapabilities, gSession->sessionPath() / L"environment_capabilities.json"))
        logEvent(L"Cannot save environment capabilities profile");

    gRunning = true;
    updateControlsForRunningState();

    std::wstring fullscreenHudError;
    gFullscreenHudActive =
        gFullscreenOverlay.ensureHelperRunning(&fullscreenHudError);

    if (gFullscreenHudActive) {
        gOverlay.hide();
        logEvent(L"Fullscreen HUD  •  ON");
    } else {
        gOverlay.showForProcess(bodycam.pid);
        logEvent(
            L"Fullscreen HUD недоступен  •  " +
            fullscreenHudError);
        logEvent(
            L"Fallback HUD  •  работает в окне/Borderless");
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
        logEvent(L"NVENC video  •  OFF");
        gRecorder.reset();
    }

    if (gAudio) {
        gAudio->stop();
        logEvent(L"Game audio  •  OFF");
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
        gEnvironmentCapabilities.sources.push_back({"gpu_last_sample", observed.gpuValid ? "observed" : "unavailable", 0, true, "GPU snapshot availability; individual counters may still be absent"});
        gTelemetry->stop();
        logEvent(L"Телеметрия  •  OFF");
        gTelemetry.reset();
    }

    const auto sessionPath = gSession->sessionPath();
    const double sessionDurationSeconds =
        std::chrono::duration<double>(
            std::chrono::steady_clock::now() - gSessionStartedAt).count();

    std::wstring muxError;
    if (muxSessionVideoWithAudio(
            gRuntimeFfmpegExe,
            sessionPath,
            &muxError)) {
        logEvent(L"Видео со звуком  •  video_with_audio.mkv");
        if (!muxError.empty())
            logEvent(L"Предупреждение очистки  •  " + muxError);
    } else {
        logEvent(L"Сведение видео/аудио пропущено  •  " + muxError);
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

    if (gGameExitedUnexpectedly) {
        const auto crashData = collectRecentBodycamCrashData(
            sessionPath,
            gSessionStartedWallClock);

        if (crashData.copiedAnything)
            logEvent(L"Crash diagnostics  •  " + crashData.details);
        else
            logEvent(L"Crash diagnostics  •  " + crashData.details);
    }

    const auto validation = validateTestSession(
        sessionPath,
        summary,
        sessionDurationSeconds,
        gGameExitedUnexpectedly);

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
    const auto bodycamLogs = collectBodycamLogs(bodycamLogRoot, sessionPath, gSessionStartedWallClock);
    const bool bodycamManifestWritten = writeBodycamLogsManifest(bodycamLogs, sessionPath / L"bodycam_logs_manifest.json");
    gEnvironmentCapabilities.sources.push_back(bodycamLogs.source);
    logEvent(bodycamManifestWritten ? L"Optional Bodycam log manifest saved" : L"Cannot save optional Bodycam log manifest");

    const auto windowsEvents = collectWindowsEvents(gSessionStartedWallClock, observationEnd);
    const bool eventReportWritten = writeWindowsEventReport(
        windowsEvents, stutterCorrelation, gPresentMonStartedWallClock,
        gSessionStartedWallClock, observationEnd, clockStable, gGameExitedUnexpectedly,
        sessionPath / L"windows_events.json");
    logEvent(eventReportWritten ? L"Windows Event Log report saved (optional evidence)" : L"Cannot save Windows Event Log report");

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

    if (!bodycamManifestWritten || (bodycamLogs.source.status != "available" && bodycamLogs.source.status != "available_empty"))
        sessionDiagnostics.diagnosticLimitations.push_back(L"Optional Bodycam logs: " + std::wstring(bodycamLogs.source.status.begin(), bodycamLogs.source.status.end()) + (bodycamManifestWritten ? L"" : L"; manifest write failed"));
    if (stutterCorrelation.videoBookmarkAlignmentAvailable)
        sessionDiagnostics.diagnosticLimitations.push_back(L"Video stutter bookmarks are navigation estimates; PresentMon first-frame and FFmpeg startup latency are not measured directly");
    else
        sessionDiagnostics.diagnosticLimitations.push_back(L"Video stutter bookmarks unavailable because stable clock and uninterrupted recorder timeline were not both confirmed");
    sessionDiagnostics.diagnosticLimitations.push_back(L"Windows Event Log temporal matches are approximate and do not prove causation; see windows_events.json");
    if (!eventReportWritten)
        sessionDiagnostics.diagnosticLimitations.push_back(L"Optional Windows Event Log report could not be written");
    for (const auto& channel : windowsEvents.channels) {
        if (channel.status != "available" && channel.status != "available_empty")
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
        gMirror->stop();
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
        -24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    gSubtitleFont = CreateFontW(
        -12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    gBodyFont = CreateFontW(
        -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    gSmallFont = CreateFontW(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    gButtonFont = CreateFontW(
        -14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
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

void paintMainWindow(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);

    RECT client{};
    GetClientRect(hwnd, &client);

    FillRect(dc, &client, gBackgroundBrush);
    SetBkMode(dc, TRANSPARENT);

    SelectObject(dc, gTitleFont);
    SetTextColor(dc, COLOR_TEXT);
    RECT titleRect{20, 10, 600, 42};
    DrawTextW(dc, L"Bodycam Diagnostics", -1, &titleRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(dc, gSmallFont);
    SetTextColor(dc, COLOR_MUTED);
    RECT presetLabel{20, 52, 600, 72};
    DrawTextW(dc, L"ПРЕСЕТ", -1, &presetLabel,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    HBRUSH panel = CreateSolidBrush(COLOR_PANEL);
    HPEN border = CreatePen(PS_SOLID, 1, COLOR_BORDER);

    auto oldBrush = SelectObject(dc, panel);
    auto oldPen = SelectObject(dc, border);

    RoundRect(dc, 20, 105, 600, 147, 10, 10);
    RoundRect(dc, 20, 224, 600, 326, 10, 10);

    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(border);
    DeleteObject(panel);

    HBRUSH dot = CreateSolidBrush(gRunning ? COLOR_OK : COLOR_ACCENT);
    RECT dotRect{34, 122, 42, 130};
    FillRect(dc, &dotRect, dot);
    DeleteObject(dot);

    SelectObject(dc, gSmallFont);
    SetTextColor(dc, COLOR_MUTED);
    RECT logLabel{20, 202, 600, 220};
    DrawTextW(dc, L"ЖУРНАЛ", -1, &logLabel,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    EndPaint(hwnd, &ps);
}

void drawButton(const DRAWITEMSTRUCT* dis) {
    if (!dis || dis->CtlType != ODT_BUTTON) return;

    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool primary = dis->hwndItem == gToggle;

    COLORREF fill = COLOR_PANEL_HOVER;
    COLORREF text = COLOR_TEXT;
    COLORREF border = COLOR_BORDER;

    if (disabled) {
        fill = COLOR_DISABLED;
        text = RGB(112, 121, 136);
        border = COLOR_DISABLED;
    } else if (primary) {
        fill = gRunning ? COLOR_STOP : COLOR_ACCENT;
        if (pressed)
            fill = gRunning ? RGB(145, 59, 69) : COLOR_ACCENT_PRESSED;
        border = fill;
    } else if (pressed) {
        fill = RGB(36, 43, 56);
    }

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);

    const auto oldBrush = SelectObject(dis->hDC, brush);
    const auto oldPen = SelectObject(dis->hDC, pen);

    RECT r = dis->rcItem;
    RoundRect(dis->hDC, r.left, r.top, r.right, r.bottom, 12, 12);

    SelectObject(dis->hDC, oldPen);
    SelectObject(dis->hDC, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);

    wchar_t textBuffer[128]{};
    GetWindowTextW(dis->hwndItem, textBuffer, static_cast<int>(std::size(textBuffer)));

    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, text);
    SelectObject(dis->hDC, gButtonFont);

    DrawTextW(
        dis->hDC,
        textBuffer,
        -1,
        &r,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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

void createControls(HWND hwnd) {
    gPreset = CreateWindowExW(
        0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
        20, 72, 580, 220,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PRESET)), nullptr, nullptr);

    SendMessageW(gPreset, WM_SETFONT, reinterpret_cast<WPARAM>(gBodyFont), TRUE);
    SendMessageW(gPreset, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), 28);
    SendMessageW(gPreset, CB_SETITEMHEIGHT, 0, 28);
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

    gStatus = CreateWindowExW(
        0, L"STATIC", L"Ожидание Bodycam...",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        52, 111, 528, 30,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)), nullptr, nullptr);
    SendMessageW(gStatus, WM_SETFONT, reinterpret_cast<WPARAM>(gBodyFont), TRUE);

    gToggle = CreateWindowW(
        L"BUTTON", L"Начать тест",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        20, 159, 180, 38,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TOGGLE)), nullptr, nullptr);

    gOpenSession = CreateWindowW(
        L"BUTTON", L"Открыть сессию",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        210, 159, 180, 38,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_OPEN_SESSION)), nullptr, nullptr);

    gCopyReport = CreateWindowW(
        L"BUTTON", L"Копировать ZIP",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        400, 159, 200, 38,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_COPY_REPORT)), nullptr, nullptr);

    gLog = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        32, 234, 556, 82,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LOG)), nullptr, nullptr);

    SendMessageW(gLog, WM_SETFONT, reinterpret_cast<WPARAM>(gSmallFont), TRUE);
    SetWindowTheme(gLog, L"DarkMode_Explorer", nullptr);

    EnableWindow(gOpenSession, FALSE);
    EnableWindow(gCopyReport, FALSE);
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

    if (gRecorder) {
        std::wstring recorderEvent;
        const bool recorderHealthy =
            gRecorder->maintain(&recorderEvent);

        if (!recorderEvent.empty()) {
            gRecorderTimelineContinuous = false;
            logEvent(recorderEvent);
        }

        if (!recorderHealthy) {
            if (recorderEvent.empty()) {
                logEvent(
                    L"Обязательный источник потерян  •  "
                    L"NVENC video unavailable");
            }

            gRecorderTimelineContinuous = false;
            gEnvironmentCapabilities.sources.push_back({
                "nvenc_video_runtime",
                "failed_during_capture",
                0,
                false,
                "Mandatory FFmpeg recorder could not continue capture"
            });

            stopSession();
            return;
        }
    }

    gLiveFrames.poll();

    const LiveFrameSnapshot frame = gLiveFrames.snapshot();
    const TelemetrySnapshot telemetry =
        gTelemetry ? gTelemetry->latest() : TelemetrySnapshot{};

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
        SetTimer(hwnd, ID_TIMER, 500, nullptr);

        appendUiLog(L"Приложение готово");
        return 0;

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
        if (control == gStatus) {
            SetTextColor(dc, COLOR_TEXT);
            SetBkColor(dc, COLOR_PANEL);
            return reinterpret_cast<INT_PTR>(gPanelBrush);
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

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, RGB(190, 200, 214));
        SetBkColor(dc, COLOR_PANEL);
        return reinterpret_cast<INT_PTR>(gPanelBrush);
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_TOGGLE:
            toggleSession();
            return 0;

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
        L"Bodycam Diagnostics",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        628, 382,
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

