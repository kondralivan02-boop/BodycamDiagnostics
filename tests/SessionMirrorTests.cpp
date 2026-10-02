#include "SessionMirror.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "SessionMirrorTests failure: "
                  << message << "\n";
        throw std::runtime_error(message);
    }
}
}

int main() {
    const auto root =
        std::filesystem::temp_directory_path() /
        L"BodycamDiagnostics-session-mirror-test";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    {
        const auto source = root / L"final-video-source";
        const auto mirrorRoot = root / L"final-video-mirror";
        std::filesystem::create_directories(source);

        std::ofstream(source / L"session.log", std::ios::binary)
            << "final video mirror evidence";
        std::ofstream(source / L"video_with_audio.mkv", std::ios::binary)
            << "muxed video with audio";
        std::ofstream(source / L"video_without_audio.mkv", std::ios::binary)
            << "finalized video without audio";
        std::filesystem::create_directories(source / L"video" / L"part_1");
        std::ofstream(source / L"video" / L"part_1" / L"video_00000.mkv", std::ios::binary)
            << "raw recording evidence";
        std::ofstream(source / L"video_part_1_video_without_audio.mkv", std::ios::binary)
            << "separate recording part";

        SessionMirror mirror(source, mirrorRoot);
        std::wstring startError;
        check(mirror.start(&startError),
              "mirror must start for final video sync");
        std::wstring stopError;
        check(mirror.stop(&stopError),
              "final mirror sync must succeed");
        check(stopError.empty(),
              "successful final mirror sync must not report an error");

        const auto target = mirror.targetPath();
        check(std::filesystem::is_regular_file(
                  target / L"video_with_audio.mkv"),
              "final video with audio must be mirrored");
        check(std::filesystem::is_regular_file(
                  target / L"video_without_audio.mkv"),
              "final video without audio must be mirrored");
        check(std::filesystem::is_regular_file(
                  target / L"video_part_1_video_without_audio.mkv"),
              "separate recording part must be mirrored");
        check(std::filesystem::is_regular_file(
                  target / L"video" / L"part_1" / L"video_00000.mkv"),
              "nested raw recording must survive through the mirror");

        {
            std::ofstream(
                target / L"video_with_audio.mkv",
                std::ios::binary | std::ios::trunc)
                << "post-stop-marker";
        }

        std::wstring secondStopError;
        check(mirror.stop(&secondStopError),
              "repeated mirror stop must return cached success");
        check(secondStopError.empty(),
              "repeated successful stop must keep empty error");

        std::ifstream markerFile(
            target / L"video_with_audio.mkv",
            std::ios::binary);
        const std::string marker{
            std::istreambuf_iterator<char>(markerFile),
            std::istreambuf_iterator<char>()};
        check(marker == "post-stop-marker",
              "repeated stop must not perform a second final sync");
    }

    {
        const auto source = root / L"health-source";
        const auto mirrorRoot = root / L"health-mirror";
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
            std::ofstream blocker(
                target,
                std::ios::binary | std::ios::trunc);
            check(static_cast<bool>(blocker),
                  "test must replace mirror directory with blocking file");
            blocker << "block target directory recreation";
        }

        std::ofstream(source / L"session.log",
                      std::ios::binary | std::ios::app)
            << " updated";

        Sleep(5500);

        std::wstring healthError;
        check(!mirror.healthy(&healthError),
              "mirror must expose a runtime copy failure");
        check(!mirror.running(),
              "mirror worker must stop after terminal runtime failure");
        check(!healthError.empty(),
              "mirror runtime failure must include diagnostics");

        std::wstring stopError;
        check(!mirror.stop(&stopError),
              "final mirror sync must expose prior runtime failure");
        check(!stopError.empty(),
              "failed final mirror sync must preserve diagnostics");
    }

    std::filesystem::remove_all(root, ec);

    std::cout << "Session mirror tests passed\n";
}
