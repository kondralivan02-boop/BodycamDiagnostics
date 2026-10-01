#include "SessionManager.h"
#include <Windows.h>

#include <chrono>
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

bool SessionManager::start(const std::wstring& presetName) {
    try {
        if (running_) stop();

        presetName_ = presetName.empty() ? L"Unnamed" : presetName;
        std::filesystem::create_directories(root_);
        sessionPath_ = root_ / (timestamp(true) + L"_" + sanitizeForFileName(presetName_));
        std::filesystem::create_directories(sessionPath_);

        logFile_.open(sessionPath_ / L"session.log",
                      std::ios::binary | std::ios::out | std::ios::app);
        if (!logFile_) return false;

        running_ = true;
        writeState(L"running");
        log(L"Session started: " + presetName_);
        return true;
    } catch (...) {
        running_ = false;
        return false;
    }
}

void SessionManager::stop() {
    if (!running_) return;

    log(L"Session stopped");
    writeState(L"stopped");
    running_ = false;

    if (logFile_.is_open())
        logFile_.close();
}

void SessionManager::writeState(const wchar_t* state) {
    if (sessionPath_.empty()) return;

    const auto target = sessionPath_ / L"session.json";
    const auto temp = sessionPath_ / L"session.json.tmp";

    std::ofstream out(temp, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out) return;

    out << "{\n"
        << "  \"state\": \"" << jsonEscapeUtf8(state) << "\",\n"
        << "  \"preset\": \"" << jsonEscapeUtf8(presetName_) << "\",\n"
        << "  \"updated_at\": \"" << jsonEscapeUtf8(timestamp(false)) << "\"\n"
        << "}\n";
    out.flush();
    out.close();

    std::error_code ec;
    std::filesystem::remove(target, ec);
    ec.clear();
    std::filesystem::rename(temp, target, ec);
}

void SessionManager::log(const std::wstring& message) {
    if (!logFile_) return;

    logFile_ << "[" << utf8(timestamp(false)) << "] " << utf8(message) << "\r\n";
    logFile_.flush();

    if (running_)
        writeState(L"running");
}
