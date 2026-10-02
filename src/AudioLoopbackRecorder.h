#pragma once

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

class AudioLoopbackRecorder {
public:
    AudioLoopbackRecorder();
    ~AudioLoopbackRecorder();

    bool start(const std::filesystem::path& wavOutput, std::wstring* error = nullptr);
    bool stop(std::wstring* error = nullptr);

    bool running() const noexcept { return running_.load(); }
    bool healthy(std::wstring* error = nullptr);

private:
    void worker();
    void signalReady(bool ok, std::wstring error = {});
    void recordRuntimeFailure(std::wstring error);

    std::filesystem::path wavOutput_;
    std::atomic_bool running_{false};
    std::atomic_bool runtimeHealthy_{true};
    std::atomic_bool stopRequested_{false};
    std::thread thread_;

    std::mutex readyMutex_;
    std::condition_variable readyCv_;
    bool ready_ = false;
    bool readyOk_ = false;
    std::wstring readyError_;
    std::wstring runtimeError_;
};
