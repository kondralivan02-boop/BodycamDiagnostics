#pragma once
#include <filesystem>
#include <fstream>
#include <string>

class SessionManager {
public:
    explicit SessionManager(std::filesystem::path root);
    ~SessionManager();

    bool start(const std::wstring& presetName);
    void stop();
    void log(const std::wstring& message);
    const std::filesystem::path& sessionPath() const noexcept { return sessionPath_; }
    bool running() const noexcept { return running_; }

private:
    void writeState(const wchar_t* state);
    static std::wstring sanitizeForFileName(std::wstring value);

    std::filesystem::path root_;
    std::filesystem::path sessionPath_;
    std::wofstream logFile_;
    std::wstring presetName_;
    bool running_ = false;
};
