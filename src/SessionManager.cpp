#include "SessionManager.h"
#include "AtomicOutputFile.h"
#include "BuildInfo.h"
#include <Windows.h>

#include <chrono>
#include <exception>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace {
std::wstring timestamp(bool filenameSafe) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);

    std::wstringstream ss;
    ss << std::put_time(&tm, filenameSafe ? L"%Y-%m-%d_%H-%M-%S" : L"%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string utf8(std::wstring_view text) {
    if (text.empty()) return {};

    const int required = WideCharToMultiByte(
        CP_UTF8, 0,
        text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);

    if (required <= 0) return {};

    std::string out(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0,
        text.data(), static_cast<int>(text.size()),
        out.data(), required, nullptr, nullptr);

    return out;
}

std::string jsonEscapeUtf8(std::wstring_view text) {
    const std::string source = utf8(text);
    std::string out;
    out.reserve(source.size() + 8);

    for (const unsigned char c : source) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char escaped[7]{};
                sprintf_s(escaped, "\\u%04x", static_cast<unsigned int>(c));
                out += escaped;
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return out;
}
}

SessionManager::SessionManager(std::filesystem::path root) : root_(std::move(root)) {}

SessionManager::~SessionManager() {
    stop();
}

std::wstring SessionManager::sanitizeForFileName(std::wstring value) {
    static constexpr wchar_t invalid[] = L"<>:\"/\\|?*";
    for (auto& c : value) {
        if (wcschr(invalid, c) || c < 32) c = L'_';
    }
    while (!value.empty() && (value.back() == L'.' || value.back() == L' ')) value.pop_back();
    if (value.empty()) value = L"Unnamed";
    if (value.size() > 80) value.resize(80);
    return value;
}

bool SessionManager::start(
    const std::wstring& presetName,
    std::wstring* error) {

    try {
        if (running_)
            stop();

        presetName_ =
            presetName.empty() ? L"Unnamed" : presetName;

        std::error_code ec;
        std::filesystem::create_directories(root_, ec);
        if (ec) {
            if (error) {
                *error =
                    L"Cannot create Sessions directory: " +
                    std::to_wstring(ec.value());
            }
            return false;
        }

        sessionPath_ =
            root_ /
            (timestamp(true) + L"_" +
             sanitizeForFileName(presetName_));

        ec.clear();
        std::filesystem::create_directories(sessionPath_, ec);
        if (ec) {
            if (error) {
                *error =
                    L"Cannot create session directory: " +
                    std::to_wstring(ec.value());
            }
            return false;
        }

        logFile_.open(
            sessionPath_ / L"session.log",
            std::ios::binary |
                std::ios::out |
                std::ios::app);

        if (!logFile_) {
            if (error)
                *error = L"Cannot open session.log";
            return false;
        }

        std::wstring buildInfoError;
        if (!writeBuildInfoReport(
                sessionPath_ / L"build_info.json",
                &buildInfoError)) {
            if (error) {
                *error =
                    L"Cannot publish build provenance: " +
                    buildInfoError;
            }
            logFile_.close();
            return false;
        }

        running_ = true;

        std::wstring stateError;
        if (!writeState(L"running", &stateError)) {
            running_ = false;
            if (error) {
                *error =
                    L"Cannot publish initial session state: " +
                    stateError;
            }
            logFile_.close();
            return false;
        }

        log(L"Session started: " + presetName_);
        return true;
    } catch (const std::exception& ex) {
        running_ = false;
        if (logFile_.is_open())
            logFile_.close();

        if (error) {
            const std::string message = ex.what();
            *error =
                L"Session initialization exception: " +
                std::wstring(
                    message.begin(),
                    message.end());
        }
        return false;
    } catch (...) {
        running_ = false;
        if (logFile_.is_open())
            logFile_.close();

        if (error)
            *error = L"Unknown session initialization exception";
        return false;
    }
}

void SessionManager::stop() {
    if (!running_)
        return;

    log(L"Session stopped");
    running_ = false;

    std::wstring stateError;
    if (!writeState(L"stopped", &stateError) &&
        logFile_.is_open()) {
        logFile_
            << "[" << utf8(timestamp(false)) << "] "
            << "Final session state publication failed: "
            << utf8(stateError)
            << "\r\n";
        logFile_.flush();
    }

    if (logFile_.is_open())
        logFile_.close();
}

bool SessionManager::writeState(
    const wchar_t* state,
    std::wstring* error) {

    if (sessionPath_.empty()) {
        if (error)
            *error = L"Session path is empty";
        return false;
    }

    AtomicOutputFile stateFile(
        sessionPath_ / L"session.json");
    auto& out = stateFile.stream();

    if (!stateFile.isOpen()) {
        if (error)
            *error = L"Cannot create temporary session state";
        return false;
    }

    out << "{\n"
        << "  \"state\": \""
        << jsonEscapeUtf8(state)
        << "\",\n"
        << "  \"preset\": \""
        << jsonEscapeUtf8(presetName_)
        << "\",\n"
        << "  \"updated_at\": \""
        << jsonEscapeUtf8(timestamp(false))
        << "\"\n"
        << "}\n";

    if (!out) {
        if (error)
            *error = L"Cannot write session state";
        return false;
    }

    return stateFile.commit(error);
}

void SessionManager::log(const std::wstring& message) {
    if (!logFile_) return;

    logFile_ << "[" << utf8(timestamp(false)) << "] " << utf8(message) << "\r\n";
    logFile_.flush();

    if (running_)
        writeState(L"running");
}
