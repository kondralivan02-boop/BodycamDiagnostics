#include "RecorderController.h"

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
          "expected immediate-exit and recovery-failure fixture paths");

    const std::filesystem::path immediate = argv[1];
    const std::filesystem::path recoveryFailure = argv[2];
    check(std::filesystem::is_regular_file(immediate),
          "immediate exit fixture must exist");
    check(std::filesystem::is_regular_file(recoveryFailure),
          "recovery failure fixture must exist");

    const auto root =
        std::filesystem::temp_directory_path() /
        L"BodycamDiagnostics-recorder-controller-test";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);

    {
        RecorderController controller(immediate);
        std::wstring error;

        const bool started =
            controller.start(root / L"video-immediate", &error);

        check(!started,
              "recorder must reject an FFmpeg process that exits during startup");
        check(!controller.running(),
              "recorder must clear process handles after startup failure");
        check(error.find(L"exited during startup") != std::wstring::npos,
              "startup failure must report the early FFmpeg exit");
    }

    {
        RecorderController controller(recoveryFailure);
        std::wstring error;

        const bool started =
            controller.start(root / L"video-recovery", &error);

        check(started,
              "recovery fixture must survive recorder startup window");
        check(controller.running(),
              "recovery fixture must be tracked after startup");

        Sleep(600);

        std::wstring event;
        check(!controller.maintain(&event),
              "recorder must report terminal failure when restart fails");
        check(!controller.running(),
              "terminal recorder failure must leave recorder unavailable");
        check(event.find(L"Захват экрана потерян") != std::wstring::npos,
              "desktop duplication loss must have a Russian explanation");
    }

    std::filesystem::remove_all(root, ec);
    std::cout << "Recorder controller tests passed\n";
}
