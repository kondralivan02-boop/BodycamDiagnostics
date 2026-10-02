#include "AudioVideoMuxer.h"

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

std::string readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}
}

int wmain(int argc, wchar_t** argv) {
    check(argc == 2, "expected mux fixture path");

    const std::filesystem::path fixture = argv[1];
    check(std::filesystem::is_regular_file(fixture),
          "mux fixture must exist");

    const auto root =
        std::filesystem::temp_directory_path() /
        L"BodycamDiagnostics-audio-video-muxer-test";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / L"video");

    {
        std::ofstream(root / L"video" / L"video_00000.mkv", std::ios::binary)
            << "raw video";
        std::ofstream(root / L"video" / L"video_capture.log", std::ios::binary)
            << "ffmpeg warning evidence";
        std::ofstream(root / L"game_audio.wav", std::ios::binary)
            << "raw audio";
    }

    std::wstring error;
    const bool ok =
        finalizeSessionVideo(
            fixture,
            root,
            true,
            &error);

    check(ok, "fake mux should complete successfully");
    check(std::filesystem::is_regular_file(root / L"video_with_audio.mkv"),
          "muxed output must remain");
    check(!std::filesystem::exists(root / L"video"),
          "raw video directory should be removed after successful mux");
    check(std::filesystem::is_regular_file(root / L"video_capture.log"),
          "FFmpeg capture log must survive raw video cleanup");
    check(readAll(root / L"video_capture.log") == "ffmpeg warning evidence",
          "preserved FFmpeg log must keep original evidence");

    const auto muxArgs =
        readAll(root / L"mux_fixture_args.txt");
    check(muxArgs.find("-af") != std::string::npos,
          "audio mux must configure an audio filter");
    check(muxArgs.find("apad") != std::string::npos,
          "short optional audio must be padded instead of truncating video");
    check(muxArgs.find("-shortest") != std::string::npos,
          "mux must still stop at the mandatory video duration");


    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / L"video");

    {
        std::ofstream(root / L"video" / L"video_00000.mkv", std::ios::binary)
            << "raw video without audio";
        std::ofstream(root / L"video" / L"video_capture.log", std::ios::binary)
            << "ffmpeg warning evidence without audio";
        std::ofstream(root / L"game_audio.wav", std::ios::binary)
            << "stale partial audio that must be ignored";
    }

    error.clear();
    const bool videoOnlyOk =
        finalizeSessionVideo(
            fixture,
            root,
            false,
            &error);

    check(videoOnlyOk,
          "video finalization must succeed when optional WASAPI audio is missing");
    check(std::filesystem::is_regular_file(root / L"video_without_audio.mkv"),
          "video-only finalization must create video_without_audio.mkv");
    check(!std::filesystem::exists(root / L"video_with_audio.mkv"),
          "audio-labelled output must not be created when audio is missing");
    check(!std::filesystem::exists(root / L"video"),
          "raw video directory should be removed after video-only finalization");
    check(std::filesystem::is_regular_file(root / L"video_capture.log"),
          "FFmpeg capture log must survive video-only finalization");
    check(readAll(root / L"video_capture.log") ==
              "ffmpeg warning evidence without audio",
          "video-only finalization must preserve FFmpeg evidence");

    std::filesystem::remove_all(root, ec);
    std::cout << "Audio/video muxer tests passed\n";
}
