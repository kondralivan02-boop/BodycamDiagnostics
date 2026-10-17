#pragma once
#include <Windows.h>
#include <filesystem>
#include <string>
#include <atomic>
#include <thread>
#include "BoundedDiagnosticOutput.h"

class PresentMonController {
public:
    explicit PresentMonController(std::filesystem::path executable);
    ~PresentMonController();
    PresentMonController(const PresentMonController&) = delete;
    PresentMonController& operator=(const PresentMonController&) = delete;

    bool available() const;
    bool start(DWORD targetPid, const std::filesystem::path& csvOutput, std::wstring* error = nullptr);
    void stop();
    bool healthy(std::wstring* event = nullptr);
    bool running() const noexcept { return process_ != nullptr; }

private:
    bool launch(const std::wstring& arguments,
                HANDLE* processOut,
                std::wstring* error,
                HANDLE stdOut = nullptr,
                HANDLE stdErr = nullptr);
    bool beginDiagnostics(const std::filesystem::path& csvOutput, HANDLE* writer, std::wstring* error);
    std::wstring finishDiagnostics();
    void readDiagnostics() noexcept;
    HANDLE stderrRead_ = nullptr;
    HANDLE stderrLog_ = INVALID_HANDLE_VALUE;
    std::thread stderrReader_;
    std::atomic<bool> stopReader_{false};
    BoundedDiagnosticOutput stderrOutput_;
    DWORD stderrReadError_ = ERROR_SUCCESS;
    std::filesystem::path executable_;
    HANDLE process_ = nullptr;
    static constexpr const wchar_t* kSessionName = L"BodycamDiagnostics";
};
