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
    void stop();
    bool running() const noexcept { return process_ != nullptr; }

private:
    std::filesystem::path ffmpegExecutable_;
    HANDLE process_ = nullptr;
    HANDLE stdinWrite_ = nullptr;
};
