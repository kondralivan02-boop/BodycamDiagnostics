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
constexpr int ID_TIMER = 1;
constexpr int HOTKEY_TOGGLE = 2001;
constexpr int HOTKEY_RECORD = 2002;

constexpr COLORREF COLOR_BG = RGB(9, 13, 22);
constexpr COLORREF COLOR_PANEL = RGB(19, 27, 40);
constexpr COLORREF COLOR_PANEL_HOVER = RGB(29, 40, 57);
constexpr COLORREF COLOR_BORDER = RGB(43, 57, 74);
constexpr COLORREF COLOR_TEXT = RGB(239, 245, 250);
constexpr COLORREF COLOR_MUTED = RGB(146, 161, 179);
constexpr COLORREF COLOR_ACCENT = RGB(62, 212, 197);
constexpr COLORREF COLOR_ACCENT_PRESSED = RGB(32, 169, 159);
constexpr COLORREF COLOR_STOP = RGB(211, 86, 106);
constexpr COLORREF COLOR_DISABLED = RGB(40, 49, 62);
constexpr COLORREF COLOR_OK = RGB(93, 217, 158);
constexpr COLORREF COLOR_SIDEBAR = RGB(15, 21, 33);
constexpr COLORREF COLOR_AMBER = RGB(244, 182, 91);
constexpr int kSidebarWidth = 246;
constexpr int kContentTop = 104;
constexpr int kMetricsTop = 334;
constexpr int kUtilitiesTop = 485;
constexpr int kLogTop = 549;

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

    SetWindowTextW(gToggle, gRunning ? L"■  ОСТАНОВИТЬ ТЕСТ  · F9"
                                    : L"▶  НАЧАТЬ ТЕСТ  · F9");
    if (gRecord) {
        SetWindowTextW(
            gRecord, gRecorder && gRecorder->running()
                ? L"■  ОСТАНОВИТЬ ВИДЕО  · F8"
                : L"●  ЗАПИСАТЬ ВИДЕО  · F8");
        EnableWindow(gRecord, gRunning);
        InvalidateRect(gRecord, nullptr, FALSE);
    }
    if (gVideoStatus) {
        const wchar_t* label = L"ВИДЕО  ·  ВЫКЛ";
        if (gRecording.state() == RecordingState::Starting) label = L"ВИДЕО  ·  ЗАПУСК";
        if (gRecording.state() == RecordingState::Recording) label = L"ВИДЕО  ·  REC";
        if (gRecording.state() == RecordingState::Lost) label = L"ВИДЕО  ·  ОШИБКА";
        if (gRecording.state() == RecordingState::Stopped) label = L"ВИДЕО  ·  СТОП";
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

    const auto runtimeRoot = appDir();
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
            appDir() / L"Sessions");

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
    gFullscreenHudActive = collection.fullscreenHelper &&
        gFullscreenOverlay.ensureHelperRunning(&fullscreenHudError);

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
        gEnvironmentCapabilities.sources.push_back({"gpu_last_sample", observed.gpuValid ? "observed" : "unavailable", 0, true, "GPU snapshot availability; individual counters may still be absent"});
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
        appDir() / L"runtime-tools.sha256";
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
        requestedVideoIntervals);

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
        resolveRuntimeTool(appDir(), L"PresentMon.exe"),
        gRuntimeFfmpegExe,
        appDir() / L"runtime-tools.sha256");
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

void paintMetric(HDC dc, const RECT& rect, const wchar_t* label,
                 const std::wstring& value, const wchar_t* hint,
                 COLORREF valueColor) {
    drawCard(dc, rect);
    drawTextLine(dc, label,
        RECT{rect.left + 18, rect.top + 15, rect.right - 10, rect.top + 38},
        gSubtitleFont, COLOR_MUTED);
    drawTextLine(dc, value.c_str(),
        RECT{rect.left + 18, rect.top + 43, rect.right - 10, rect.top + 95},
        gTitleFont, valueColor);
    drawTextLine(dc, hint,
        RECT{rect.left + 18, rect.bottom - 32, rect.right - 10, rect.bottom - 12},
        gSmallFont, COLOR_MUTED);
}

void paintDashboard(HDC dc, int width, int height) {
    const RECT all{0, 0, width, height};
    fillRegion(dc, all, COLOR_BG);
    fillRegion(dc, RECT{0, 0, kSidebarWidth, height}, COLOR_SIDEBAR);
    fillRegion(dc, RECT{kSidebarWidth - 1, 0, kSidebarWidth, height}, COLOR_BORDER);
    fillRegion(dc, RECT{0, 0, 5, height}, COLOR_ACCENT);

    // Identity / navigation sidebar.
    drawCard(dc, RECT{23, 25, 66, 68}, COLOR_ACCENT);
    drawTextLine(dc, L"BD", RECT{25, 27, 65, 66},
                 gButtonFont, COLOR_BG, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    drawTextLine(dc, L"BODYCAM", RECT{78, 25, 221, 50},
                 gSubtitleFont, COLOR_TEXT);
    drawTextLine(dc, L"DIAGNOSTICS", RECT{78, 45, 232, 68},
                 gSmallFont, COLOR_MUTED);

    drawTextLine(dc, L"РАБОЧАЯ ПАНЕЛЬ", RECT{23, 102, 226, 125},
                 gSubtitleFont, COLOR_ACCENT);
    drawTextLine(dc, L"01  /  МОНИТОРИНГ", RECT{23, 143, 226, 166},
                 gSmallFont, COLOR_TEXT);
    fillRegion(dc, RECT{23, 177, 221, 178}, COLOR_BORDER);
    drawTextLine(dc, L"КОНФИГУРАЦИЯ ТЕСТА", RECT{23, 222, 225, 249},
                 gSubtitleFont, COLOR_MUTED);
    drawTextLine(dc, L"СОВМЕСТИМОСТЬ", RECT{23, 322, 222, 341},
                 gSubtitleFont, COLOR_MUTED);

    const COLORREF sidebarIndicator = gRunning ? COLOR_OK : COLOR_MUTED;
    drawCircle(dc, 23, height - 126, 9, sidebarIndicator);
    drawTextLine(dc, gRunning ? L"ДИАГНОСТИКА АКТИВНА" : L"ОЖИДАНИЕ ЗАПУСКА",
        RECT{40, height - 136, 225, height - 111},
        gSubtitleFont, gRunning ? COLOR_OK : COLOR_MUTED);
    drawTextLine(dc, L"LOCAL  /  WIN64", RECT{23, height - 98, 224, height - 78},
                 gSmallFont, COLOR_MUTED);

    const int x = kSidebarWidth + 32;
    const int right = width - 32;
    const int available = right - x;

    drawTextLine(dc, L"Панель диагностики", RECT{x, 22, right - 166, 62},
                 gTitleFont, COLOR_TEXT);
    drawTextLine(dc, L"Bodycam / DLSS / Frame Generation",
        RECT{x, 65, right - 80, 87}, gSmallFont, COLOR_MUTED);

    drawCard(dc, RECT{right - 143, 31, right, 68},
             gRunning ? RGB(22, 54, 49) : COLOR_PANEL);
    drawCircle(dc, right - 130, 46, 9, gRunning ? COLOR_OK : COLOR_MUTED);
    drawTextLine(dc, gRunning ? L"LIVE SESSION" : L"STANDBY",
        RECT{right - 113, 35, right - 7, 64},
        gSubtitleFont, gRunning ? COLOR_OK : COLOR_MUTED);

    // Session state and recording state remain ordinary Win32 STATIC controls.
    drawCard(dc, RECT{x, kContentTop, right, 229});
    drawCircle(dc, x + 22, kContentTop + 33, 12,
        gRunning ? COLOR_OK : COLOR_ACCENT);
    drawTextLine(dc, L"СТАТУС ТЕСТА",
        RECT{x + 44, kContentTop + 15, right - 20, kContentTop + 44},
        gSubtitleFont, COLOR_MUTED);
    drawTextLine(dc, L"УПРАВЛЕНИЕ  /  F9: ТЕСТ     F8: ВИДЕО",
        RECT{x + 22, kContentTop + 92, right - 25, kContentTop + 115},
        gSmallFont, COLOR_MUTED);

    drawTextLine(dc, L"ПОКАЗАТЕЛИ В РЕАЛЬНОМ ВРЕМЕНИ",
        RECT{x, 306, right, 332}, gSubtitleFont, COLOR_MUTED);

    const int gap = 12;
    const int cell = (available - 3 * gap) / 4;
    const RECT metric1{x, kMetricsTop, x + cell, kMetricsTop + 135};
    const RECT metric2{x + cell + gap, kMetricsTop,
                       x + 2 * cell + gap, kMetricsTop + 135};
    const RECT metric3{x + 2 * (cell + gap), kMetricsTop,
                       x + 3 * cell + 2 * gap, kMetricsTop + 135};
    const RECT metric4{x + 3 * (cell + gap), kMetricsTop,
                       right, kMetricsTop + 135};

    paintMetric(dc, metric1, L"FPS",
        gDashboardFrame.valid ? compactValue(gDashboardFrame.fps) : L"—",
        L"частота кадров", COLOR_ACCENT);
    paintMetric(dc, metric2, L"FRAMETIME",
        gDashboardFrame.valid
            ? compactValue(gDashboardFrame.frameTimeMs, 1) + L" ms" : L"—",
        L"время кадра", COLOR_TEXT);
    paintMetric(dc, metric3, L"1% LOW",
        gDashboardFrame.valid
            ? compactValue(gDashboardFrame.onePercentLowFps) : L"—",
        L"плавность", COLOR_OK);
    paintMetric(dc, metric4, L"GPU LOAD",
        gDashboardTelemetry.gpuValid
            ? std::to_wstring(gDashboardTelemetry.gpuUtilPct) + L"%" : L"—",
        L"загрузка видеокарты", COLOR_AMBER);

    // Persistent log occupies remaining vertical space on resized windows.
    drawCard(dc, RECT{x, kLogTop, right, height - 26});
    drawCircle(dc, x + 19, kLogTop + 20, 8, COLOR_ACCENT);
    drawTextLine(dc, L"ЖУРНАЛ СОБЫТИЙ",
        RECT{x + 36, kLogTop + 8, right - 20, kLogTop + 40},
        gSubtitleFont, COLOR_TEXT);
    drawTextLine(dc, L"События теста и сообщения диагностики",
        RECT{x + 20, height - 62, right - 20, height - 36},
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
    const bool primary = dis->hwndItem == gToggle;
    const bool recording = gRecorder && gRecorder->running();

    COLORREF fill = COLOR_PANEL_HOVER;
    COLORREF textColor = COLOR_TEXT;
    COLORREF stroke = COLOR_BORDER;

    if (disabled) {
        fill = COLOR_DISABLED;
        textColor = COLOR_MUTED;
        stroke = COLOR_DISABLED;
    } else if (primary) {
        fill = gRunning ? COLOR_STOP : COLOR_ACCENT;
        textColor = COLOR_BG;
        stroke = fill;
    } else if (dis->hwndItem == gRecord) {
        fill = recording ? COLOR_STOP : RGB(64, 45, 34);
        stroke = recording ? COLOR_STOP : COLOR_AMBER;
        textColor = recording ? COLOR_TEXT : COLOR_AMBER;
    }
    if (pressed && !disabled)
        fill = primary ? COLOR_ACCENT_PRESSED : RGB(36, 52, 66);

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, stroke);
    const auto oldBrush = SelectObject(dis->hDC, brush);
    const auto oldPen = SelectObject(dis->hDC, pen);
    RECT r = dis->rcItem;
    RoundRect(dis->hDC, r.left, r.top, r.right, r.bottom, 16, 16);
    SelectObject(dis->hDC, oldPen);
    SelectObject(dis->hDC, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);

    wchar_t caption[160]{};
    GetWindowTextW(dis->hwndItem, caption,
                   static_cast<int>(std::size(caption)));
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, textColor);
    const auto oldFont = SelectObject(dis->hDC, gButtonFont);
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
    const int height = client.bottom;
    const int x = kSidebarWidth + 32;
    const int right = client.right - 32;
    const int available = right - x;
    if (available <= 0) return;

    const int sideButtonWidth = kSidebarWidth - 46;
    if (gPreset) MoveWindow(gPreset, 23, 254, sideButtonWidth, 250, TRUE);
    if (gGizmo) MoveWindow(gGizmo, 23, 350, sideButtonWidth, 42, TRUE);
    if (gHelp) MoveWindow(gHelp, 23, height - 69, sideButtonWidth, 40, TRUE);

    if (gStatus) MoveWindow(gStatus, x + 23, kContentTop + 45,
                            available - 248, 37, TRUE);
    if (gVideoStatus) MoveWindow(gVideoStatus, right - 200,
                                 kContentTop + 45, 173, 37, TRUE);
    const int actionGap = 12;
    const int actionWidth = (available - actionGap) / 2;
    if (gToggle) MoveWindow(gToggle, x, 244, actionWidth, 54, TRUE);
    if (gRecord) MoveWindow(gRecord, x + actionWidth + actionGap, 244,
                            available - actionWidth - actionGap, 54, TRUE);
    if (gOpenSession) MoveWindow(gOpenSession, x, kUtilitiesTop,
                                actionWidth, 46, TRUE);
    if (gCopyReport) MoveWindow(gCopyReport,
                               x + actionWidth + actionGap, kUtilitiesTop,
                               available - actionWidth - actionGap, 46, TRUE);

    if (gLog) MoveWindow(gLog, x + 19, kLogTop + 45,
                         available - 38, (std::max)(90, height - kLogTop - 115),
                         TRUE);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void createControls(HWND hwnd) {
    gPreset = CreateWindowExW(
        0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
        0, 0, 200, 250,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PRESET)), nullptr, nullptr);

    SendMessageW(gPreset, WM_SETFONT, reinterpret_cast<WPARAM>(gBodyFont), TRUE);
    SendMessageW(gPreset, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), 34);
    SendMessageW(gPreset, CB_SETITEMHEIGHT, 0, 34);
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
        0, 0, 150, 30, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)), nullptr, nullptr);
    SendMessageW(gStatus, WM_SETFONT, reinterpret_cast<WPARAM>(gBodyFont), TRUE);

    gVideoStatus = CreateWindowW(
        L"STATIC", L"ВИДЕО  ·  ВЫКЛ",
        WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
        0, 0, 150, 30, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_VIDEO_STATUS)), nullptr, nullptr);
    SendMessageW(gVideoStatus, WM_SETFONT, reinterpret_cast<WPARAM>(gSubtitleFont), TRUE);

    gToggle = CreateWindowW(
        L"BUTTON", L"▶  НАЧАТЬ ТЕСТ  · F9",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 200, 54, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TOGGLE)), nullptr, nullptr);
    gRecord = CreateWindowW(
        L"BUTTON", L"●  ЗАПИСАТЬ ВИДЕО  · F8",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 200, 54, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_RECORD)), nullptr, nullptr);
    gOpenSession = CreateWindowW(
        L"BUTTON", L"ОТКРЫТЬ ПОСЛЕДНЮЮ СЕССИЮ",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 200, 46, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_OPEN_SESSION)), nullptr, nullptr);
    gCopyReport = CreateWindowW(
        L"BUTTON", L"КОПИРОВАТЬ ПУТЬ К ZIP",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 200, 46, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_COPY_REPORT)), nullptr, nullptr);

    gGizmo = CreateWindowW(L"BUTTON", L"Gizmo / совместимость",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 200, 42, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_GIZMO)), nullptr, nullptr);
    SendMessageW(gGizmo, WM_SETFONT, reinterpret_cast<WPARAM>(gBodyFont), TRUE);
    SetWindowTheme(gGizmo, L"DarkMode_Explorer", nullptr);

    gHelp = CreateWindowW(L"BUTTON", L"СПРАВКА",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 200, 40, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_HELP)), nullptr, nullptr);

    gLog = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        0, 0, 200, 150, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LOG)), nullptr, nullptr);
    SendMessageW(gLog, WM_SETFONT, reinterpret_cast<WPARAM>(gSmallFont), TRUE);
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
        return 0;

    case WM_GETMINMAXINFO: {
        auto* size = reinterpret_cast<MINMAXINFO*>(lp);
        size->ptMinTrackSize = POINT{1010, 725};
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

        case ID_RECORD:
            toggleRecording();
            return 0;

        case ID_HELP: {
            const auto help = appDir() / L"СПРАВКА.txt";
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
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1120, 795,
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
