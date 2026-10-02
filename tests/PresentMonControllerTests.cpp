#include "PresentMonController.h"

#include <Windows.h>

#include <filesystem>
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
    check(argc == 3,
          "expected immediate-exit and delayed-exit fixture paths");

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

    std::filesystem::remove_all(root, ec);
    std::cout << "PresentMon controller tests passed\n";
}
