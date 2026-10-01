#pragma once
#include <Windows.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

struct TelemetrySnapshot {
    bool valid = false;
    bool gpuValid = false;
    double processCpuPct = 0.0;
    double systemCpuPct = 0.0;
    double workingSetMiB = 0.0;
    double privateUsageMiB = 0.0;
    double processReadMiBps = 0.0;
    double processWriteMiBps = 0.0;
    double systemMemoryLoadPct = 0.0;
    double systemMemoryAvailableMiB = 0.0;
    unsigned gpuUtilPct = 0;
    unsigned gpuMemoryUtilPct = 0;
    unsigned long long vramUsedMiB = 0;
    unsigned long long vramTotalMiB = 0;
    unsigned gpuTempC = 0;
    unsigned gpuGraphicsMHz = 0;
    unsigned gpuMemoryMHz = 0;
    double gpuPowerW = 0.0;
    double gpuPowerLimitW = 0.0;
};

class TelemetrySampler {
public:
    TelemetrySampler();
    ~TelemetrySampler();

    bool start(DWORD targetPid, const std::filesystem::path& csvOutput, std::wstring* error = nullptr);
    void stop();
    bool running() const noexcept { return running_.load(); }
    TelemetrySnapshot latest() const;

private:
    void worker();

    DWORD targetPid_ = 0;
    HANDLE process_ = nullptr;
    std::filesystem::path csvOutput_;
    std::atomic_bool running_{false};
    std::thread thread_;

    mutable std::mutex snapshotMutex_;
    TelemetrySnapshot latest_;
};
