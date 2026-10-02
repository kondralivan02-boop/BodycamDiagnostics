#pragma once
#include <Windows.h>
#include <filesystem>
#include <string>

class PresentMonController {
public:
    explicit PresentMonController(std::filesystem::path executable);
    ~PresentMonController();

    bool available() const;
    bool start(DWORD targetPid, const std::filesystem::path& csvOutput, std::wstring* error = nullptr);
    void stop();
    bool healthy(std::wstring* event = nullptr);
    bool running() const noexcept { return process_ != nullptr; }

private:
    bool launch(const std::wstring& arguments,
                HANDLE* processOut,
                std::wstring* error,
                HANDLE stdOut = nullptr);
    std::filesystem::path executable_;
    HANDLE process_ = nullptr;
    static constexpr const wchar_t* kSessionName = L"BodycamDiagnostics";
};
