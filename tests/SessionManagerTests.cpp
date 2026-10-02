#include "SessionManager.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}

std::string readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string{
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()};
}
}

int main() {
    const auto root =
        std::filesystem::temp_directory_path() /
        L"BodycamDiagnostics-session-manager-test";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    SessionManager session(root);
    std::wstring startError;
    check(session.start(L"Atomic State Test", &startError),
          "session must start");
    check(startError.empty(),
          "successful session start must not report an error");

    const auto buildInfoPath =
        session.sessionPath() / L"build_info.json";
    check(std::filesystem::is_regular_file(buildInfoPath),
          "session start must publish build_info.json");

    const auto buildInfo = readAll(buildInfoPath);
    check(buildInfo.find("\"commit_sha\"") != std::string::npos,
          "build provenance must include commit SHA");
    check(buildInfo.find("\"branch\"") != std::string::npos,
          "build provenance must include branch");
    check(buildInfo.find("\"ci_run\"") != std::string::npos,
          "build provenance must include CI run");

    const auto statePath =
        session.sessionPath() / L"session.json";
    const auto tempPath =
        session.sessionPath() / L"session.json.tmp";

    check(std::filesystem::is_regular_file(statePath),
          "running session.json must exist");
    check(!std::filesystem::exists(tempPath),
          "temporary state file must not remain after publication");

    const auto running = readAll(statePath);
    check(running.find("\"state\": \"running\"") != std::string::npos,
          "running state must be published");
    check(running.find("Atomic State Test") != std::string::npos,
          "preset must be published");

    session.log(L"Regression evidence");
    check(!std::filesystem::exists(tempPath),
          "logging must leave no temporary state file");

    session.stop();

    const auto stopped = readAll(statePath);
    check(stopped.find("\"state\": \"stopped\"") != std::string::npos,
          "stopped state must replace running state");
    check(!std::filesystem::exists(tempPath),
          "stopped publication must leave no temporary state file");

    const auto blocker = root / L"blocked-root";
    {
        std::ofstream(blocker, std::ios::binary | std::ios::trunc)
            << "not a directory";
    }

    SessionManager blocked(blocker / L"Sessions");
    std::wstring blockedError;
    check(!blocked.start(L"Blocked", &blockedError),
          "blocked session root must fail");
    check(!blockedError.empty(),
          "session initialization failure must explain its cause");

    std::filesystem::remove_all(root, ec);
    std::cout << "Session manager tests passed\n";
}
