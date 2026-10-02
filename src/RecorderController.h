#pragma once
#include <Windows.h>
#include <filesystem>
#include <string>

class RecorderController {
public:
    explicit RecorderController(std::filesystem::path ffmpegExecutable);
    ~RecorderController();

    bool available() const;
    bool start(const std::filesystem::path& outputDirectory, std::wstring* error = nullptr);
    bool maintain(std::wstring* event = nullptr);
    void stop();
    bool running() const noexcept { return process_ != nullptr && !disabled_; }

private:
    bool launchCapture(std::wstring* error);
    unsigned nextSegmentNumber() const;
    void closeChildHandles();

    std::filesystem::path ffmpegExecutable_;
    std::filesystem::path outputDirectory_;
    HANDLE process_ = nullptr;
    HANDLE stdinWrite_ = nullptr;
    bool disabled_ = false;
};
