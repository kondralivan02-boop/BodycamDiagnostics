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
    void stop();

    bool running() const noexcept { return running_.load(); }
    const std::filesystem::path& targetPath() const noexcept { return targetSession_; }

private:
    void worker();
    void syncOnce(bool finalPass);
    void copyRelativeFile(const std::filesystem::path& relative);

    std::filesystem::path sourceSession_;
    std::filesystem::path targetSession_;
    std::atomic_bool running_{false};
    std::thread thread_;
    std::map<std::filesystem::path, std::uintmax_t> previousVideoSizes_;
};
