#include "TestValidator.h"

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
    std::string out;
    out.reserve(text.size());
    for (wchar_t c : text)
        out.push_back(c >= 0 && c <= 0x7F ? static_cast<char>(c) : '?');
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
    return appDir / L"Tools" / L"ffmpeg.exe";
}
}

TestValidationResult validateTestSession(
    const std::filesystem::path& sessionPath,
    const MetricsSummary& summary,
    double expectedDurationSeconds,
    bool gameExitedUnexpectedly) {

    TestValidationResult result;
    result.expectedDurationSeconds = expectedDurationSeconds;

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
    const auto video = sessionPath / L"video_with_audio.mkv";
    if (!std::filesystem::is_regular_file(video, ec) ||
        std::filesystem::file_size(video, ec) == 0) {
        result.issues.push_back(L"Final video_with_audio.mkv is missing or empty");
    } else if (expectedDurationSeconds > 1.0) {
        result.videoDurationChecked = probeVideoDurationSeconds(
            defaultFfmpegPath(sessionPath),
            video,
            sessionPath,
            result.videoDurationSeconds);

        if (result.videoDurationChecked) {
            result.videoCoverageRatio =
                result.videoDurationSeconds / expectedDurationSeconds;

            if (result.videoCoverageRatio < 0.90) {
                std::wstringstream ss;
                ss << L"Final video covers only "
                   << std::fixed << std::setprecision(1)
                   << result.videoCoverageRatio * 100.0
                   << L"% of the test duration";
                result.issues.push_back(ss.str());
            }
        } else {
            result.issues.push_back(
                L"Final video duration could not be verified");
        }
    }

    ec.clear();
    const auto telemetry = sessionPath / L"telemetry.csv";
    if (!std::filesystem::is_regular_file(telemetry, ec) ||
        std::filesystem::file_size(telemetry, ec) == 0) {
        result.issues.push_back(L"telemetry.csv is missing or empty");
    }

    result.valid = result.issues.empty();
    return result;
}

bool writeTestValidationReport(
    const TestValidationResult& r,
    const std::filesystem::path& jsonOutput,
    const std::filesystem::path& textOutput,
    std::wstring* error) {

    std::ofstream json(jsonOutput, std::ios::binary | std::ios::trunc);
    if (!json) {
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
         << "  \"video_duration_seconds\": "
         << r.videoDurationSeconds << ",\n"
         << "  \"video_coverage_ratio\": "
         << r.videoCoverageRatio << ",\n"
         << "  \"issues\": [";

    for (std::size_t i = 0; i < r.issues.size(); ++i) {
        if (i) json << ", ";
        json << "\"" << jsonEscape(narrowAscii(r.issues[i])) << "\"";
    }

    json << "]\n}\n";

    std::ofstream text(textOutput, std::ios::binary | std::ios::trunc);
    if (!text) {
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
         << "Video coverage: "
         << r.videoCoverageRatio * 100.0 << "%\n";

    for (const auto& issue : r.issues)
        text << "- " << narrowAscii(issue) << "\n";

    return true;
}
