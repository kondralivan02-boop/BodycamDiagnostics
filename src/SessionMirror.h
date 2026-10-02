#pragma once

#include <atomic>
#include <filesystem>
#include <map>
#include <string>
#include <thread>

class SessionMirror {
public:
    SessionMirror(std::filesystem::path sourceSession,
                  std::filesystem::path mirrorRoot);
    ~SessionMirror();

    bool start(std::wstring* error = nullptr);
    bool stop(std::wstring* error = nullptr);

    bool running() const noexcept { return running_.load(); }
    bool healthy(std::wstring* error = nullptr) const;
    const std::filesystem::path& targetPath() const noexcept { return targetSession_; }

private:
    void worker();
    bool syncOnce(bool finalPass);
    bool copyRelativeFile(const std::filesystem::path& relative);
    void recordFailure(int errorCode);

    std::filesystem::path sourceSession_;
    std::filesystem::path targetSession_;
    std::atomic_bool running_{false};
    std::atomic_int lastErrorCode_{0};
    std::thread thread_;
    std::map<std::filesystem::path, std::uintmax_t> previousVideoSizes_;
    bool stopCompleted_ = false;
    bool lastStopResult_ = true;
    std::wstring lastStopError_;
};
