#include "SessionMirror.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
}

int main() {
    const auto root =
        std::filesystem::temp_directory_path() /
        L"BodycamDiagnostics-session-mirror-test";
    const auto source = root / L"source-session";
    const auto mirrorRoot = root / L"mirror-root";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(source);

    std::ofstream(source / L"session.log", std::ios::binary)
        << "initial evidence";

    SessionMirror mirror(source, mirrorRoot);
    std::wstring startError;
    check(mirror.start(&startError),
          "mirror must start on a writable target");
    check(mirror.healthy(),
          "freshly started mirror must be healthy");

    Sleep(300);

    const auto target = mirror.targetPath();
    std::filesystem::remove_all(target, ec);
    ec.clear();

    {
        std::ofstream blocker(target, std::ios::binary | std::ios::trunc);
        check(static_cast<bool>(blocker),
              "test must replace mirror directory with blocking file");
        blocker << "block target directory recreation";
    }

    std::ofstream(source / L"session.log", std::ios::binary | std::ios::app)
        << " updated";

    Sleep(5500);

    std::wstring healthError;
    check(!mirror.healthy(&healthError),
          "mirror must expose a runtime copy failure");
    check(!mirror.running(),
          "mirror worker must stop after terminal runtime failure");
    check(!healthError.empty(),
          "mirror runtime failure must include diagnostics");

    mirror.stop();
    std::filesystem::remove_all(root, ec);

    std::cout << "Session mirror tests passed\n";
}
