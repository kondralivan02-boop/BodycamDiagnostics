#pragma once
#include <Windows.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>

class TelemetrySampler {
public:
    TelemetrySampler();
    ~TelemetrySampler();

    bool start(DWORD targetPid, const std::filesystem::path& csvOutput, std::wstring* error = nullptr);
    void stop();
    bool running() const noexcept { return running_.load(); }

private:
    void worker();

    DWORD targetPid_ = 0;
    HANDLE process_ = nullptr;
    std::filesystem::path csvOutput_;
    std::atomic_bool running_{false};
    std::thread thread_;
};
