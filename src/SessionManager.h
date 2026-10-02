#pragma once
#include <filesystem>
#include <fstream>
#include <string>

class SessionManager {
public:
    explicit SessionManager(std::filesystem::path root);
    ~SessionManager();

    bool start(
        const std::wstring& presetName,
        std::wstring* error = nullptr);
    void stop();
    void log(const std::wstring& message);
    const std::filesystem::path& sessionPath() const noexcept { return sessionPath_; }
    bool running() const noexcept { return running_; }

private:
    bool writeState(
        const wchar_t* state,
        std::wstring* error = nullptr);
    static std::wstring sanitizeForFileName(std::wstring value);

    std::filesystem::path root_;
    std::filesystem::path sessionPath_;
    std::ofstream logFile_;
    std::wstring presetName_;
    bool running_ = false;
};
