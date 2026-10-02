#include "SessionMirror.h"
#include "AtomicOutputFile.h"

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

    lastErrorCode_.store(0);

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

bool SessionMirror::healthy(std::wstring* error) const {
    const int errorCode = lastErrorCode_.load();
    if (errorCode == 0)
        return true;

    if (error) {
        *error =
            L"Persistent mirror runtime failure: " +
            std::to_wstring(errorCode);
    }
    return false;
}

void SessionMirror::recordFailure(int errorCode) {
    if (errorCode == 0)
        errorCode = static_cast<int>(std::errc::io_error);

    int expected = 0;
    lastErrorCode_.compare_exchange_strong(
        expected,
        errorCode);
}

bool SessionMirror::copyRelativeFile(const std::filesystem::path& relative) {
    const auto source = sourceSession_ / relative;
    const auto target = targetSession_ / relative;

    std::error_code ec;
    const bool sourceExists =
        std::filesystem::exists(source, ec);

    if (ec) {
        if (ec == std::errc::no_such_file_or_directory)
            return true;

        recordFailure(ec.value());
        return false;
    }

    if (!sourceExists)
        return true;

    ec.clear();
    if (!std::filesystem::is_regular_file(source, ec)) {
        if (ec) {
            recordFailure(ec.value());
            return false;
        }
        return true;
    }

    ec.clear();
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) {
        recordFailure(ec.value());
        return false;
    }

    std::wstring copyError;
    if (!atomicCopyFile(
            source,
            target,
            &copyError)) {
        recordFailure(ERROR_WRITE_FAULT);
        return false;
    }

    return true;
}

bool SessionMirror::syncOnce(bool finalPass) {
    static constexpr const wchar_t* kLightweightFiles[] = {
        L"build_info.json",
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

    for (const auto* name : kLightweightFiles) {
        if (!copyRelativeFile(name))
            return false;
    }

    if (finalPass) {
        static constexpr const wchar_t* kFinalOnlyFiles[] = {
            L"video_with_audio.mkv",
            L"video_without_audio.mkv"
        };

        for (const auto* name : kFinalOnlyFiles) {
            if (!copyRelativeFile(name))
                return false;
        }
    }

    const auto videoDir = sourceSession_ / L"video";
    std::error_code ec;
    const bool videoDirExists =
        std::filesystem::exists(videoDir, ec);

    if (ec) {
        if (ec == std::errc::no_such_file_or_directory)
            return true;

        recordFailure(ec.value());
        return false;
    }

    if (!videoDirExists)
        return true;

    ec.clear();
    if (!std::filesystem::is_directory(videoDir, ec)) {
        if (ec) {
            recordFailure(ec.value());
            return false;
        }
        return true;
    }

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

        if (needsCopy &&
            !copyRelativeFile(relative)) {
            return false;
        }
    }

    if (ec) {
        recordFailure(ec.value());
        return false;
    }

    return true;
}

void SessionMirror::worker() {
    while (running_.load()) {
        if (!syncOnce(false)) {
            running_.store(false);
            return;
        }

        for (int i = 0; i < 10 && running_.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

