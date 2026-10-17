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

    check(recordingPartDirectory(root, 1) == root / L"video" / L"part_1",
          "recording interval directory must match the muxer");
    check(recordingPartAudioPath(root, 2) ==
              root / L"video" / L"part_2" / L"game_audio.wav",
          "WASAPI and muxer must use the same per-interval WAV path");

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
          "audio-labelled output must not be created when audio is excluded");
    check(!std::filesystem::exists(root / L"game_audio.wav"),
          "excluded partial audio must not remain as a normal WAV artifact");
    check(!std::filesystem::exists(root / L"video"),
          "raw video directory should be removed after video-only finalization");
    check(std::filesystem::is_regular_file(root / L"video_capture.log"),
          "FFmpeg capture log must survive video-only finalization");
    check(readAll(root / L"video_capture.log") ==
              "ffmpeg warning evidence without audio",
          "video-only finalization must preserve FFmpeg evidence");

    std::filesystem::create_directories(root / L"video" / L"part_1");
    std::filesystem::create_directories(root / L"video" / L"part_2");
    std::ofstream(root / L"video" / L"part_1" / L"video_00000.mkv") << "first interval";
    std::ofstream(root / L"video" / L"part_2" / L"video_00000.mkv") << "second interval";
    check(finalizeRecordingPart(fixture, root, 1, false, &error), "first interval must finalize");
    check(finalizeRecordingPart(fixture, root, 2, false, &error), "second interval must finalize");
    check(std::filesystem::is_regular_file(root / L"video_part_1_video_without_audio.mkv"),
          "first recording interval must have a distinct video");
    check(std::filesystem::is_regular_file(root / L"video_part_2_video_without_audio.mkv"),
          "second recording interval must have a distinct video");

    // Regression: each independently toggled recording interval must consume its own WAV.
    // A session-level game_audio.wav would be truncated by the next WASAPI start.
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / L"video" / L"part_1");
    std::filesystem::create_directories(root / L"video" / L"part_2");
    std::ofstream(root / L"video" / L"part_1" / L"video_00000.mkv") << "first interval";
    std::ofstream(root / L"video" / L"part_2" / L"video_00000.mkv") << "second interval";
    std::ofstream(root / L"video" / L"part_1" / L"game_audio.wav") << "first audio";
    std::ofstream(root / L"video" / L"part_2" / L"game_audio.wav") << "second audio";

    error.clear();
    check(finalizeRecordingPart(fixture, root, 1, true, &error),
          "first interval with per-part audio must finalize");
    check(readAll(root / L"mux_fixture_args.txt").find("part_1") != std::string::npos,
          "first interval mux must read its own WAV");
    check(finalizeRecordingPart(fixture, root, 2, true, &error),
          "second interval with per-part audio must finalize");
    const auto secondMuxArgs = readAll(root / L"mux_fixture_args.txt");
    check(secondMuxArgs.find("part_2") != std::string::npos &&
              secondMuxArgs.find("part_1") == std::string::npos,
          "second interval mux must not reuse first interval audio");
    check(std::filesystem::is_regular_file(root / L"video_part_1_video_with_audio.mkv"),
          "first interval must retain its own audio");
    check(std::filesystem::is_regular_file(root / L"video_part_2_video_with_audio.mkv"),
          "second interval must retain its own audio");

    // A failed optional audio interval must not block video finalization.
    std::filesystem::create_directories(recordingPartDirectory(root, 3));
    std::ofstream(recordingPartDirectory(root, 3) / L"video_00000.mkv")
        << "third interval, no audio";
    error.clear();
    check(finalizeRecordingPart(fixture, root, 3, false, &error),
          "failed optional audio must not prevent video finalization");
    check(std::filesystem::is_regular_file(root / L"video_part_3_video_without_audio.mkv"),
          "interval without audio must remain independently usable");

    // Even an unexpected missing WAV must not prevent video-only muxing.
    std::filesystem::create_directories(recordingPartDirectory(root, 4));
    std::ofstream(recordingPartDirectory(root, 4) / L"video_00000.mkv")
        << "fourth interval, WAV absent";
    error.clear();
    check(finalizeRecordingPart(fixture, root, 4, true, &error),
          "missing audio file must fall back to video-only");
    check(std::filesystem::is_regular_file(root / L"video_part_4_video_without_audio.mkv"),
          "video must survive even when audio was requested but absent");

    // An FFmpeg audio-mux failure must retry video-only with the raw source.
    std::filesystem::create_directories(recordingPartDirectory(root, 5));
    std::ofstream(recordingPartDirectory(root, 5) / L"video_00000.mkv")
        << "fifth interval, playable video";
    std::ofstream(recordingPartAudioPath(root, 5)) << "corrupt WAV";
    std::ofstream(recordingPartDirectory(root, 5) / L"video_capture.log")
        << "audio failure capture evidence";
    std::ofstream(root / L"mux_fixture_fail_audio.txt") << "force failure";
    error.clear();
    check(finalizeRecordingPart(fixture, root, 5, true, &error),
          "audio mux failure must fall back to video-only");
    check(std::filesystem::is_regular_file(root / L"mux_fixture_audio_attempted.txt"),
          "test must exercise and fail the audio mux attempt");
    check(std::filesystem::is_regular_file(root / L"video_part_5_video_without_audio.mkv"),
          "video-only fallback must be saved");
    check(!std::filesystem::exists(root / L"video_part_5_video_with_audio.mkv"),
          "failed audio output must not look like a valid video");
    check(error.find(L"video saved without audio") != std::wstring::npos,
          "audio failure must be reported as a warning");
    check(readAll(root / L"video_part_5_capture.log") ==
              "audio failure capture evidence",
          "capture evidence must be preserved after fallback");
    check(!std::filesystem::exists(recordingPartDirectory(root, 5)),
          "raw video should only be removed after successful fallback");
    std::filesystem::remove(root / L"mux_fixture_fail_audio.txt", ec);

    // When both mux attempts fail, keep the original raw segments for recovery.
    std::filesystem::create_directories(recordingPartDirectory(root, 6));
    std::ofstream(recordingPartDirectory(root, 6) / L"video_00000.mkv")
        << "sixth interval raw video";
    std::ofstream(recordingPartAudioPath(root, 6)) << "unusable audio";
    std::ofstream(root / L"mux_fixture_fail_all.txt") << "force all failure";
    error.clear();
    check(!finalizeRecordingPart(fixture, root, 6, true, &error),
          "failed audio and video mux must return failure");
    check(std::filesystem::is_regular_file(
              recordingPartDirectory(root, 6) / L"video_00000.mkv"),
          "raw video must survive failed fallback");
    check(error.find(L"video-only fallback failed") != std::wstring::npos,
          "both failed mux attempts must be diagnosed");
    std::filesystem::remove(root / L"mux_fixture_fail_all.txt", ec);

    // FFmpeg may exit 0 and still leave a zero-byte audio mux output.
    // This is an audio failure and must trigger the video-only retry.
    std::filesystem::create_directories(recordingPartDirectory(root, 7));
    std::ofstream(recordingPartDirectory(root, 7) / L"video_00000.mkv")
        << "seventh interval raw video";
    std::ofstream(recordingPartAudioPath(root, 7)) << "WAV fixture";
    std::ofstream(root / L"mux_fixture_empty_audio.txt") << "force empty mux";
    error.clear();
    check(finalizeRecordingPart(fixture, root, 7, true, &error),
          "empty successful audio mux must fall back to video-only");
    check(std::filesystem::is_regular_file(root / L"video_part_7_video_without_audio.mkv"),
          "fallback must produce non-empty video without audio");
    check(std::filesystem::file_size(root / L"video_part_7_video_without_audio.mkv") > 0,
          "fallback output must not be empty");
    check(!std::filesystem::exists(root / L"video_part_7_video_with_audio.mkv"),
          "zero-byte audio output must be discarded");
    check(error.find(L"video saved without audio") != std::wstring::npos,
          "empty audio mux recovery must be reported");
    std::filesystem::remove(root / L"mux_fixture_empty_audio.txt", ec);

    // Failure to preserve a capture log must not delete its only copy.
    std::filesystem::create_directories(recordingPartDirectory(root, 8));
    std::ofstream(recordingPartDirectory(root, 8) / L"video_00000.mkv")
        << "eighth interval raw video";
    std::ofstream(recordingPartDirectory(root, 8) / L"video_capture.log")
        << "original FFmpeg failure evidence";
    std::filesystem::create_directory(root / L"video_part_8_capture.log");
    error.clear();
    check(finalizeRecordingPart(fixture, root, 8, false, &error),
          "video should finalize even when capture-log preservation fails");
    check(std::filesystem::is_regular_file(root / L"video_part_8_video_without_audio.mkv"),
          "final video must remain usable");
    check(readAll(recordingPartDirectory(root, 8) / L"video_capture.log") ==
              "original FFmpeg failure evidence",
          "raw capture log must survive a failed copy");
    check(error.find(L"Raw recording retained") != std::wstring::npos,
          "report must explain why raw video was preserved");

    std::filesystem::remove_all(root, ec);
    std::cout << "Audio/video muxer tests passed\n";
}
