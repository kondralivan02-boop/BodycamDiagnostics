#include "PresentMonController.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
}

int wmain(int argc, wchar_t** argv) {
    check(argc == 4,
          "expected immediate, delayed and diagnostic fixture paths");

    const std::filesystem::path immediate = argv[1];
    const std::filesystem::path delayed = argv[2];
    check(std::filesystem::is_regular_file(immediate),
          "immediate exit fixture must exist");
    check(std::filesystem::is_regular_file(delayed),
          "delayed exit fixture must exist");

    const auto root =
        std::filesystem::temp_directory_path() /
        L"BodycamDiagnostics-presentmon-controller-test";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);

    {
        PresentMonController controller(immediate);
        std::wstring error;

        const bool started =
            controller.start(
                GetCurrentProcessId(),
                root / L"presentmon-immediate.csv",
                &error);

        check(!started,
              "controller must reject a PresentMon process that exits during startup");
        check(!controller.running(),
              "controller must clear process handle after startup failure");
        check(error.find(L"exited during startup") != std::wstring::npos,
              "startup failure must report the early process exit");
    }

    {
        PresentMonController controller(delayed);
        std::wstring error;

        const bool started =
            controller.start(
                GetCurrentProcessId(),
                root / L"presentmon-delayed.csv",
                &error);

        check(started,
              "delayed fixture must survive PresentMon startup window");
        check(controller.running(),
              "delayed fixture must be tracked after startup");

        Sleep(600);

        std::wstring event;
        check(!controller.healthy(&event),
              "controller must detect PresentMon death during capture");
        check(!controller.running(),
              "health failure must clear dead PresentMon handle");
        check(event.find(L"exited during capture") != std::wstring::npos,
              "capture failure must report PresentMon exit");
    }

    const auto read = [](const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    const std::filesystem::path diagnostic = argv[3];
    for (DWORD pid : {DWORD{6001}, DWORD{6002}, DWORD{6003}}) {
        const auto csv = root / (L"diagnostic-" + std::to_wstring(pid) + L".csv");
        PresentMonController controller(diagnostic);
        std::wstring error;
        const auto before = GetTickCount64();
        bool started = controller.start(pid, csv, &error);
        if (started) {
            const auto deadline = GetTickCount64() + 8000;
            while (GetTickCount64() < deadline && controller.healthy(&error)) Sleep(10);
        }
        check(GetTickCount64() - before < 10000, "noisy stderr must not block the child or controller");
        check(!controller.running(), "diagnostic fixture process released");
        check(error.find(L"code 6") != std::wstring::npos, "exit code 6 retained");
        check(error.find(L"ETW access denied") != std::wstring::npos, "stderr retained in failure reason");
        const auto csvText = read(csv);
        check(csvText == "Application,ProcessID,FrameTime\r\nfixture,6,16.0\r\n", "stdout CSV remains isolated and intact");
        const auto log = std::filesystem::path(csv.wstring() + L".stderr.log");
        const auto text = read(log);
        check(text.find("ETW access denied") != std::string::npos, "bounded stderr log persists diagnostics");
        check(text.size() <= 16 * 1024 + 64, "stderr log cannot grow without bound");
        check(error.size() <= 4096 + 1024, "displayed stderr is bounded");
        if (pid == 6003) check(text.find("truncated") != std::string::npos, "flood truncation reported");
    }
    {
        const auto csv = root / L"blocked-diagnostics.csv";
        const auto log = std::filesystem::path(csv.wstring() + L".stderr.log");
        std::ofstream(log) << "previous evidence";
        std::ofstream(csv) << "previous CSV";
        PresentMonController controller(diagnostic);
        std::wstring error;
        check(!controller.start(6001, csv, &error), "existing diagnostic log refuses overwrite");
        check(!error.empty(), "diagnostic setup failure must be explained");
        check(read(log) == "previous evidence", "existing diagnostic evidence preserved");
        check(read(csv) == "previous CSV", "diagnostic setup refusal must not truncate existing CSV");
    }
    {
        const auto blockedCsv = root / L"csv-is-directory";
        std::filesystem::create_directory(blockedCsv);
        PresentMonController controller(diagnostic); std::wstring error;
        check(!controller.start(6001, blockedCsv, &error), "CSV open failure must clean up diagnostic reader");
        check(error.find(L"Cannot create PresentMon CSV") != std::wstring::npos, "CSV failure reports original error");
        check(!controller.start(6001, root / L"retry.csv", &error), "retry fixture exits deliberately");
        check(error.find(L"code 6") != std::wstring::npos && error.find(L"ETW access denied") != std::wstring::npos,
              "controller can capture fresh stderr after a setup failure");
    }
    {
        PresentMonController controller(diagnostic); std::wstring error;
        const auto csv = root / L"normal-stop.csv";
        check(controller.start(6002, csv, &error), "normal-stop fixture starts");
        controller.stop();
        check(!controller.running(), "normal stop releases controller");
        check(read(std::filesystem::path(csv.wstring() + L".stderr.log")).find("ETW access denied") != std::string::npos,
              "normal stop also persists bounded stderr");
    }
    {
        const auto invalid = root / L"invalid.exe"; std::ofstream(invalid) << "not an executable";
        PresentMonController controller(invalid); std::wstring error;
        check(!controller.start(6001, root / L"invalid.csv", &error), "invalid executable fails without leaking a reader");
        check(!error.empty() && !controller.running(), "launch failure is explicit");
    }
    std::filesystem::remove_all(root, ec);
    std::cout << "PresentMon controller tests passed\n";
}
