#include "TestValidator.h"
#include "AtomicOutputFile.h"
#include "ToolPreflight.h"

#include <Windows.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::string narrowAscii(const std::wstring& text) {
    if (text.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string out(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), bytes, nullptr, nullptr);
    return out;
}

std::string jsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

std::wstring quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

double parseSeconds(const std::string& value) {
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(value.c_str(), &end);
    if (errno != 0 || end == value.c_str())
        return 0.0;
    return parsed;
}

bool parseFfmpegDuration(const std::string& text, double& seconds) {
    const std::string marker = "Duration: ";
    const auto pos = text.find(marker);
    if (pos == std::string::npos)
        return false;

    const auto start = pos + marker.size();
    if (start + 8 > text.size())
        return false;

    const std::string hh = text.substr(start, 2);
    const std::string mm = text.substr(start + 3, 2);

    auto secondEnd = text.find(',', start + 6);
    if (secondEnd == std::string::npos)
        secondEnd = text.find('\n', start + 6);
    if (secondEnd == std::string::npos)
        return false;

    const std::string ss = text.substr(start + 6, secondEnd - (start + 6));

    seconds =
        parseSeconds(hh) * 3600.0 +
        parseSeconds(mm) * 60.0 +
        parseSeconds(ss);

    return seconds > 0.0;
}

bool probeVideoDurationSeconds(
    const std::filesystem::path& ffmpegExe,
    const std::filesystem::path& videoFile,
    const std::filesystem::path& sessionPath,
    double& durationSeconds) {

    std::error_code ec;
    if (!std::filesystem::is_regular_file(ffmpegExe, ec) ||
        !std::filesystem::is_regular_file(videoFile, ec)) {
        return false;
    }

    const auto probeOutput = sessionPath / L"video_duration_probe.txt";
    ec.clear();
    std::filesystem::remove(probeOutput, ec);

    HANDLE logFile = CreateFileW(
        probeOutput.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY,
        nullptr);

    if (logFile == INVALID_HANDLE_VALUE)
        return false;

    SetHandleInformation(logFile, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    const std::wstring command =
        quote(ffmpegExe.wstring()) +
        L" -hide_banner -i " +
        quote(videoFile.wstring());

    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = logFile;
    si.hStdError = logFile;

    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(
        ffmpegExe.c_str(),
        mutableCommand.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        sessionPath.c_str(),
        &si,
        &pi);

    if (!ok) {
        CloseHandle(logFile);
        std::filesystem::remove(probeOutput, ec);
        return false;
    }

    CloseHandle(pi.hThread);
    const DWORD wait = WaitForSingleObject(pi.hProcess, 15000);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 6);
        WaitForSingleObject(pi.hProcess, 1000);
    }

    CloseHandle(pi.hProcess);
    CloseHandle(logFile);

    std::ifstream in(probeOutput, std::ios::binary);
    std::string output(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());

    ec.clear();
    std::filesystem::remove(probeOutput, ec);

    return parseFfmpegDuration(output, durationSeconds);
}

std::filesystem::path defaultFfmpegPath(
    const std::filesystem::path& sessionPath) {

    const auto sessionsDir = sessionPath.parent_path();
    const auto appDir = sessionsDir.parent_path();
    return resolveRuntimeTool(
        appDir,
        L"ffmpeg.exe");
}

std::filesystem::path finalVideoPath(
    const std::filesystem::path& sessionPath,
    bool& videoHasAudio) {

    const auto withAudio =
        sessionPath / L"video_with_audio.mkv";
    std::error_code ec;
    if (std::filesystem::is_regular_file(withAudio, ec)) {
        videoHasAudio = true;
        return withAudio;
    }

    ec.clear();
    const auto withoutAudio =
        sessionPath / L"video_without_audio.mkv";
    if (std::filesystem::is_regular_file(withoutAudio, ec)) {
        videoHasAudio = false;
        return withoutAudio;
    }

    videoHasAudio = false;
    return {};
}

std::filesystem::path partVideoPath(const std::filesystem::path& sessionPath,
                                    std::size_t part,
                                    bool& hasAudio) {
    const auto prefix = L"video_part_" + std::to_wstring(part) + L"_";
    std::error_code ec;
    auto path = sessionPath / (prefix + L"video_with_audio.mkv");
    if (std::filesystem::is_regular_file(path, ec)) { hasAudio = true; return path; }
    path = sessionPath / (prefix + L"video_without_audio.mkv");
    hasAudio = false;
    if (std::filesystem::is_regular_file(path, ec)) return path;
    return {};
}
}

TestValidationResult validateTestSession(
    const std::filesystem::path& sessionPath,
    const MetricsSummary& summary,
    double expectedDurationSeconds,
    bool gameExitedUnexpectedly,
    bool ffmpegTrustedForProbe,
    double requestedVideoSeconds,
    bool videoCaptureFailed,
    const std::vector<double>& requestedIntervals) {

    TestValidationResult result;
    result.expectedDurationSeconds = expectedDurationSeconds;
    result.requestedVideoSeconds = requestedVideoSeconds;
    if (videoCaptureFailed)
        result.issues.push_back(L"Запрошенная запись видео прервалась или не запустилась");

    if (gameExitedUnexpectedly)
        result.issues.push_back(L"Bodycam exited or crashed during the test");

    if (!summary.valid) {
        result.issues.push_back(L"PresentMon metrics are unavailable");
    } else if (expectedDurationSeconds > 1.0) {
        result.presentMonCoverageRatio =
            summary.durationSeconds / expectedDurationSeconds;

        if (result.presentMonCoverageRatio < 0.90) {
            std::wstringstream ss;
            ss << L"PresentMon captured only "
               << std::fixed << std::setprecision(1)
               << result.presentMonCoverageRatio * 100.0
               << L"% of the test duration";
            result.issues.push_back(ss.str());
        }
    }

    std::error_code ec;
    bool videoHasAudio = false;
    const auto video =
        finalVideoPath(sessionPath, videoHasAudio);

    ec.clear();
    if (!requestedIntervals.empty()) {
        result.videoDurationChecked = ffmpegTrustedForProbe;
        for (std::size_t index = 0; index < requestedIntervals.size(); ++index) {
            bool hasAudio = false;
            const auto part = partVideoPath(sessionPath, index + 1, hasAudio);
            ec.clear();
            if (part.empty() || !std::filesystem::is_regular_file(part, ec) ||
                std::filesystem::file_size(part, ec) == 0) {
                result.issues.push_back(L"Отсутствует фрагмент видео " + std::to_wstring(index + 1));
                result.videoDurationChecked = false;
                continue;
            }
            if (!hasAudio) result.warnings.push_back(L"Фрагмент видео без игрового звука");
            if (!ffmpegTrustedForProbe) {
                result.issues.push_back(L"FFmpeg не проверен; длительность фрагмента неизвестна");
                result.videoDurationChecked = false;
                continue;
            }
            double duration = 0.0;
            if (!probeVideoDurationSeconds(defaultFfmpegPath(sessionPath), part, sessionPath, duration)) {
                result.issues.push_back(L"Не удалось измерить длительность фрагмента " + std::to_wstring(index + 1));
                result.videoDurationChecked = false;
                continue;
            }
            result.videoDurationSeconds += duration;
            if (requestedIntervals[index] > 0.0 && duration / requestedIntervals[index] < 0.90)
                result.issues.push_back(L"Фрагмент видео " + std::to_wstring(index + 1) + L" короче запрошенного интервала");
        }
        if (requestedVideoSeconds > 0.0)
            result.videoCoverageRatio = result.videoDurationSeconds / requestedVideoSeconds;
    } else if (requestedVideoSeconds <= 0.0 && !videoCaptureFailed) {
        result.warnings.push_back(L"Видео выключено пользователем; диагностический тест выполнен без записи");
    } else if (requestedVideoSeconds > 0.0 && (video.empty() ||
        !std::filesystem::is_regular_file(video, ec) ||
        std::filesystem::file_size(video, ec) == 0)) {
        result.issues.push_back(
            L"Final video is missing or empty");
    } else if (requestedVideoSeconds > 0.0) {
        if (!videoHasAudio) {
            result.warnings.push_back(
                L"Final video has no WASAPI audio; video capture remains valid");
        }

        if (requestedVideoSeconds > 1.0) {
            if (!ffmpegTrustedForProbe) {
                result.issues.push_back(
                    L"Final video duration was not probed because FFmpeg trust revalidation failed");
            } else {
                result.videoDurationChecked =
                    probeVideoDurationSeconds(
                        defaultFfmpegPath(sessionPath),
                        video,
                        sessionPath,
                        result.videoDurationSeconds);

                if (result.videoDurationChecked) {
                    result.videoCoverageRatio =
                        result.videoDurationSeconds /
                        requestedVideoSeconds;

                    if (result.videoCoverageRatio < 0.90) {
                        std::wstringstream ss;
                        ss << L"Final video covers only "
                           << std::fixed
                           << std::setprecision(1)
                           << result.videoCoverageRatio * 100.0
                           << L"% of the test duration";
                        result.issues.push_back(ss.str());
                    }
                } else {
                    result.issues.push_back(
                        L"Final video duration could not be verified");
                }
            }
        }
    }

    ec.clear();
    const auto telemetry = sessionPath / L"telemetry.csv";
    if (!std::filesystem::is_regular_file(telemetry, ec) ||
        std::filesystem::file_size(telemetry, ec) == 0) {
        result.warnings.push_back(L"Optional telemetry.csv is missing or empty; telemetry correlation is limited");
    }

    result.valid = result.issues.empty();
    return result;
}

bool writeTestValidationReport(
    const TestValidationResult& r,
    const std::filesystem::path& jsonOutput,
    const std::filesystem::path& textOutput,
    std::wstring* error) {

    AtomicOutputFile jsonFile(jsonOutput);
    auto& json = jsonFile.stream();
    if (!jsonFile.isOpen()) {
        if (error) *error = L"Cannot create validation.json";
        return false;
    }

    json << std::fixed << std::setprecision(3)
         << "{\n"
         << "  \"valid\": " << (r.valid ? "true" : "false") << ",\n"
         << "  \"expected_duration_seconds\": "
         << r.expectedDurationSeconds << ",\n"
         << "  \"presentmon_coverage_ratio\": "
         << r.presentMonCoverageRatio << ",\n"
         << "  \"video_duration_checked\": "
         << (r.videoDurationChecked ? "true" : "false") << ",\n"
         << "  \"requested_video_seconds\": " << r.requestedVideoSeconds << ",\n"
         << "  \"video_duration_seconds\": "
         << r.videoDurationSeconds << ",\n"
         << "  \"video_coverage_ratio\": "
         << r.videoCoverageRatio << ",\n"
         << "  \"issues\": [";

    for (std::size_t i = 0; i < r.issues.size(); ++i) {
        if (i) json << ", ";
        json << "\"" << jsonEscape(narrowAscii(r.issues[i])) << "\"";
    }

    json << "],\n  \"warnings\": [";
    for (std::size_t i = 0; i < r.warnings.size(); ++i) {
        if (i) json << ", ";
        json << "\"" << jsonEscape(narrowAscii(r.warnings[i])) << "\"";
    }
    json << "]\n}\n";

    AtomicOutputFile textFile(textOutput);
    auto& text = textFile.stream();
    if (!textFile.isOpen()) {
        if (error) *error = L"Cannot create validation.txt";
        return false;
    }

    text << (r.valid ? "VALID TEST\n" : "INVALID TEST\n")
         << std::fixed << std::setprecision(2)
         << "Expected duration: " << r.expectedDurationSeconds << " s\n"
         << "PresentMon coverage: "
         << r.presentMonCoverageRatio * 100.0 << "%\n"
         << "Video duration checked: "
         << (r.videoDurationChecked ? "yes" : "no") << "\n"
         << "Video duration: "
         << r.videoDurationSeconds << " s\n"
         << "Requested video: " << r.requestedVideoSeconds << " s\n"
         << "Video coverage: " << r.videoCoverageRatio * 100.0 << "%\n";

    for (const auto& issue : r.issues)
        text << "- " << narrowAscii(issue) << "\n";
    for (const auto& warning : r.warnings)
        text << "Warning: " << narrowAscii(warning) << "\n";

    if (!json) {
        if (error)
            *error = L"Failed while writing validation.json";
        return false;
    }

    if (!text) {
        if (error)
            *error = L"Failed while writing validation.txt";
        return false;
    }

    if (!jsonFile.commit(error))
        return false;
    if (!textFile.commit(error))
        return false;

    return true;
}
