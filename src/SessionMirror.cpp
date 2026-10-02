#include "SessionMirror.h"

#include <chrono>
#include <system_error>

SessionMirror::SessionMirror(std::filesystem::path sourceSession,
                             std::filesystem::path mirrorRoot)
    : sourceSession_(std::move(sourceSession)),
      targetSession_(std::move(mirrorRoot) / sourceSession_.filename()) {}

SessionMirror::~SessionMirror() {
    stop();
}

bool SessionMirror::start(std::wstring* error) {
    if (running()) return true;

    std::error_code ec;
    std::filesystem::create_directories(targetSession_, ec);
    if (ec) {
        if (error) *error = L"Cannot create mirror directory: " + std::to_wstring(ec.value());
        return false;
    }

    running_.store(true);
    try {
        thread_ = std::thread(&SessionMirror::worker, this);
    } catch (...) {
        running_.store(false);
        if (error) *error = L"Cannot start mirror thread";
        return false;
    }
    return true;
}

void SessionMirror::stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
    syncOnce(true);
}

void SessionMirror::copyRelativeFile(const std::filesystem::path& relative) {
    const auto source = sourceSession_ / relative;
    const auto target = targetSession_ / relative;

    std::error_code ec;
    if (!std::filesystem::is_regular_file(source, ec)) return;

    ec.clear();
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) return;

    ec.clear();
    std::filesystem::copy_file(source, target,
                               std::filesystem::copy_options::overwrite_existing, ec);
}

void SessionMirror::syncOnce(bool finalPass) {
    static constexpr const wchar_t* kLightweightFiles[] = {
        L"bodycam_logs_manifest.json",
        L"environment_capabilities.json",
        L"windows_events.json",
        L"session_diagnostics.json",
        L"session_diagnostics.txt",
        L"session.log",
        L"session.json",
        L"system.json",
        L"presentmon.csv",
        L"telemetry.csv",
        L"video\\video_capture.log",
        L"video_capture.log",
        L"summary.json",
        L"summary.txt",
        L"report_bundle.zip"
    };

    for (const auto* name : kLightweightFiles)
        copyRelativeFile(name);

    const auto videoDir = sourceSession_ / L"video";
    std::error_code ec;
    if (!std::filesystem::is_directory(videoDir, ec)) return;

    for (std::filesystem::directory_iterator it(videoDir, ec), end; !ec && it != end; it.increment(ec)) {
        const auto& entry = *it;
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != L".mkv") continue;

        const auto relative = std::filesystem::path(L"video") / entry.path().filename();
        const auto size = entry.file_size(ec);
        if (ec || size == 0) {
            ec.clear();
            continue;
        }

        bool stable = finalPass;
        const auto previous = previousVideoSizes_.find(relative);
        if (!finalPass && previous != previousVideoSizes_.end() && previous->second == size)
            stable = true;

        previousVideoSizes_[relative] = size;
        if (!stable) continue;

        const auto target = targetSession_ / relative;
        bool needsCopy = true;
        ec.clear();
        if (std::filesystem::is_regular_file(target, ec)) {
            ec.clear();
            const auto targetSize = std::filesystem::file_size(target, ec);
            if (!ec && targetSize == size) needsCopy = false;
        }
        ec.clear();

        if (needsCopy)
            copyRelativeFile(relative);
    }
}

void SessionMirror::worker() {
    while (running_.load()) {
        syncOnce(false);

        for (int i = 0; i < 10 && running_.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

