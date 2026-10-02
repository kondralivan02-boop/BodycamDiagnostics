#include "RecorderController.h"

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
    check(argc == 2, "expected exit-fixture executable path");

    const std::filesystem::path fixture = argv[1];
    check(std::filesystem::is_regular_file(fixture),
          "exit fixture must exist");

    const auto root =
        std::filesystem::temp_directory_path() /
        L"BodycamDiagnostics-recorder-controller-test";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);

    RecorderController controller(fixture);
    std::wstring error;

    const bool started =
        controller.start(root / L"video", &error);

    check(!started,
          "recorder must reject an FFmpeg process that exits during startup");
    check(!controller.running(),
          "recorder must clear process handles after startup failure");
    check(error.find(L"exited during startup") != std::wstring::npos,
          "startup failure must report the early FFmpeg exit");

    controller.stop();
    std::filesystem::remove_all(root, ec);

    std::cout << "Recorder controller tests passed\n";
}
