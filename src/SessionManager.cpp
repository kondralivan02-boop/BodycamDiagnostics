#include "SessionManager.h"
#include <chrono>
#include <iomanip>
#include <sstream>

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

std::wstring jsonEscape(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size() + 8);
    for (wchar_t c : text) {
        switch (c) {
        case L'\\': out += L"\\\\"; break;
        case L'\"': out += L"\\\""; break;
        case L'\n': out += L"\\n"; break;
        case L'\r': out += L"\\r"; break;
        case L'\t': out += L"\\t"; break;
        default: out += c; break;
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
        logFile_.open(sessionPath_ / L"session.log", std::ios::out | std::ios::app);
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
    if (logFile_.is_open()) logFile_.close();
}

void SessionManager::writeState(const wchar_t* state) {
    if (sessionPath_.empty()) return;
    const auto target = sessionPath_ / L"session.json";
    const auto temp = sessionPath_ / L"session.json.tmp";

    std::wofstream out(temp, std::ios::out | std::ios::trunc);
    if (!out) return;
    out << L"{\n"
        << L"  \"state\": \"" << jsonEscape(state) << L"\",\n"
        << L"  \"preset\": \"" << jsonEscape(presetName_) << L"\",\n"
        << L"  \"updated_at\": \"" << jsonEscape(timestamp(false)) << L"\"\n"
        << L"}\n";
    out.flush();
    out.close();

    std::error_code ec;
    std::filesystem::remove(target, ec);
    ec.clear();
    std::filesystem::rename(temp, target, ec);
}

void SessionManager::log(const std::wstring& message) {
    if (!logFile_) return;
    logFile_ << L"[" << timestamp(false) << L"] " << message << L"\n";
    logFile_.flush();
    if (running_) writeState(L"running");
}
