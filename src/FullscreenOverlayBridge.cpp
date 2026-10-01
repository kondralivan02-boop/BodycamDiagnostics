#include "FullscreenOverlayBridge.h"

#include <Shellapi.h>

#include <cstring>

namespace {
std::filesystem::path programFilesDirectory() {
    wchar_t buffer[2048]{};
    const DWORD count = GetEnvironmentVariableW(
        L"ProgramFiles", buffer, static_cast<DWORD>(std::size(buffer)));

    if (count > 0 && count < std::size(buffer))
        return std::filesystem::path(buffer);

    return L"C:\\Program Files";
}
}

FullscreenOverlayBridge::~FullscreenOverlayBridge() {
    shutdown();
}

bool FullscreenOverlayBridge::initialize(std::wstring* error) {
    if (state_)
        return true;

    mapping_ = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(sizeof(OverlaySharedState)),
        kOverlaySharedMemoryName);

    if (!mapping_) {
        if (error)
            *error = L"CreateFileMapping failed: " +
                     std::to_wstring(GetLastError());
        return false;
    }

    const bool created = GetLastError() != ERROR_ALREADY_EXISTS;

    state_ = static_cast<OverlaySharedState*>(
        MapViewOfFile(
            mapping_,
            FILE_MAP_ALL_ACCESS,
            0, 0,
            sizeof(OverlaySharedState)));

    if (!state_) {
        if (error)
            *error = L"MapViewOfFile failed: " +
                     std::to_wstring(GetLastError());
        CloseHandle(mapping_);
        mapping_ = nullptr;
        return false;
    }

    if (created)
        std::memset(state_, 0, sizeof(*state_));

    beginWrite();
    state_->shutdown = 0;
    state_->visible = 0;
    state_->ownerPid = GetCurrentProcessId();
    state_->targetPid = 0;
    endWrite();

    return true;
}

std::filesystem::path FullscreenOverlayBridge::installedHelperPath() {
    return programFilesDirectory() /
           L"BodycamDiagnostics" /
           L"BodycamFullscreenOverlay.exe";
}

bool FullscreenOverlayBridge::ensureHelperRunning(std::wstring* error) {
    if (!state_ && !initialize(error))
        return false;

    if (helperProcess_) {
        if (WaitForSingleObject(helperProcess_, 0) == WAIT_TIMEOUT)
            return true;

        CloseHandle(helperProcess_);
        helperProcess_ = nullptr;
    }

    const auto helper = installedHelperPath();
    if (!std::filesystem::exists(helper)) {
        if (error) {
            *error =
                L"Fullscreen HUD helper is not installed. "
                L"Run EnableFullscreenOverlay.cmd once as administrator.";
        }
        return false;
    }

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = helper.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        if (error)
            *error = L"Fullscreen HUD helper launch failed: " +
                     std::to_wstring(GetLastError());
        return false;
    }

    helperProcess_ = sei.hProcess;

    const DWORD waitResult = WaitForSingleObject(helperProcess_, 300);
    if (waitResult == WAIT_OBJECT_0) {
        DWORD exitCode = 0;
        GetExitCodeProcess(helperProcess_, &exitCode);
        CloseHandle(helperProcess_);
        helperProcess_ = nullptr;

        if (error) {
            if (exitCode == 5) {
                *error =
                    L"Fullscreen HUD helper started without Windows UIAccess. "
                    L"Run EnableFullscreenOverlay.cmd as administrator.";
            } else {
                *error =
                    L"Fullscreen HUD helper exited immediately. Code: " +
                    std::to_wstring(exitCode);
            }
        }
        return false;
    }

    return true;
}

void FullscreenOverlayBridge::beginWrite() {
    if (!state_) return;
    InterlockedIncrement(&state_->sequence);
    MemoryBarrier();
}

void FullscreenOverlayBridge::endWrite() {
    if (!state_) return;
    MemoryBarrier();
    InterlockedIncrement(&state_->sequence);
}

void FullscreenOverlayBridge::publish(
    DWORD targetPid,
    const LiveFrameSnapshot& frame,
    const TelemetrySnapshot& telemetry) {

    if (!state_)
        return;

    beginWrite();

    state_->shutdown = 0;
    state_->visible = 1;
    state_->ownerPid = GetCurrentProcessId();
    state_->targetPid = targetPid;

    state_->frameValid = frame.valid ? 1 : 0;
    state_->totalFrames =
        static_cast<unsigned long long>(frame.totalFrames);
    state_->fps = frame.fps;
    state_->frameTimeMs = frame.frameTimeMs;
    state_->onePercentLowFps = frame.onePercentLowFps;
    state_->pointOnePercentLowFps = frame.pointOnePercentLowFps;

    state_->telemetryValid = telemetry.valid ? 1 : 0;
    state_->gpuValid = telemetry.gpuValid ? 1 : 0;
    state_->processCpuPct = telemetry.processCpuPct;
    state_->workingSetMiB = telemetry.workingSetMiB;
    state_->gpuUtilPct = telemetry.gpuUtilPct;
    state_->gpuMemoryUtilPct = telemetry.gpuMemoryUtilPct;
    state_->vramUsedMiB = telemetry.vramUsedMiB;
    state_->vramTotalMiB = telemetry.vramTotalMiB;
    state_->gpuTempC = telemetry.gpuTempC;
    state_->gpuGraphicsMHz = telemetry.gpuGraphicsMHz;
    state_->gpuMemoryMHz = telemetry.gpuMemoryMHz;

    endWrite();
}

void FullscreenOverlayBridge::hide() {
    if (!state_)
        return;

    beginWrite();
    state_->visible = 0;
    state_->targetPid = 0;
    endWrite();
}

void FullscreenOverlayBridge::shutdown() {
    if (state_) {
        beginWrite();
        state_->visible = 0;
        state_->shutdown = 1;
        endWrite();
    }

    if (helperProcess_) {
        CloseHandle(helperProcess_);
        helperProcess_ = nullptr;
    }

    if (state_) {
        UnmapViewOfFile(state_);
        state_ = nullptr;
    }

    if (mapping_) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
}
