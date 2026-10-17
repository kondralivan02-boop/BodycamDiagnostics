#include "AppStorage.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

namespace {
constexpr wchar_t kDataRootVariable[] = L"BODYCAM_DIAG_DATA_ROOT";

struct RestoreDataRoot {
    bool wasSet = false;
    std::wstring original;

    RestoreDataRoot() {
        SetLastError(ERROR_SUCCESS);
        const DWORD required = GetEnvironmentVariableW(kDataRootVariable, nullptr, 0);
        if (required == 0) {
            wasSet = GetLastError() != ERROR_ENVVAR_NOT_FOUND;
        } else {
            std::vector<wchar_t> buffer(required);
            const DWORD copied =
                GetEnvironmentVariableW(kDataRootVariable, buffer.data(), required);
            check(copied > 0 && copied < required, "read original data root");
            original.assign(buffer.data(), copied);
            wasSet = true;
        }
    }

    ~RestoreDataRoot() {
        SetEnvironmentVariableW(
            kDataRootVariable, wasSet ? original.c_str() : nullptr);
    }

    void set(const wchar_t* value) {
        check(SetEnvironmentVariableW(kDataRootVariable, value) != 0,
              "set data root environment variable");
    }
};

void expectInvalidDataRoot(
    RestoreDataRoot& env,
    const wchar_t* override,
    const std::filesystem::path& appRoot) {

    env.set(override);
    std::wstring error;
    check(userDataDirectory(&error).empty(), "invalid data root must fail closed");
    check(!error.empty(), "invalid data root must provide an error");
    error.clear();
    check(writableSessionRoot(appRoot, true, &error).empty(),
          "invalid single EXE session root must not fall back to LocalAppData");
    check(!error.empty(), "invalid session root must provide an error");
}
} // namespace

int main() {
    RestoreDataRoot env;
    env.set(nullptr);

    std::wstring error;
    const auto user = userDataDirectory(&error);
    check(!user.empty(), "default user LocalAppData resolves without elevation");

    const auto root = std::filesystem::temp_directory_path() /
        (L"Bodycam-storage-test-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);

    check(writableSessionRoot(root, true, &error) == user / L"Sessions",
          "default single-file data remains in user profile");
    check(writableSessionRoot(root, false, &error) == root / L"Sessions",
          "ordinary build retains writable portable sessions");

    std::filesystem::remove_all(root / L"Sessions", ec);
    std::ofstream(root / L"Sessions") << "existing file must not be overwritten";
    check(writableSessionRoot(root, false, &error) == user / L"Sessions",
          "blocked portable sessions folder falls back to profile");
    check(std::filesystem::is_regular_file(root / L"Sessions"),
          "portable fallback preserves existing user file");

    // The test uses the current machine's writable drive; production users
    // explicitly set the same variable to D:\BodycamDiagnosticsData.
    const auto selected = root / L"custom-data";
    env.set(selected.c_str());
    check(userDataDirectory(&error) == selected,
          "single EXE uses explicit data root rather than LocalAppData");
    check(writableSessionRoot(root, true, &error) == selected / L"Sessions",
          "single EXE sessions use the same explicit root as runtime");
    check(std::filesystem::is_directory(selected / L"Sessions"),
          "configured Sessions directory is created");
    check(writableSessionRoot(root, false, &error) == selected / L"Sessions",
          "ordinary build fallback honors the configured root too");

    expectInvalidDataRoot(env, L"relative\\storage", root);
    expectInvalidDataRoot(env, L"C:relative\\storage", root);
    expectInvalidDataRoot(env, L"\\\\server\\share\\Bodycam", root);
    expectInvalidDataRoot(env, L"D:\\", root);
    expectInvalidDataRoot(env, L"D:\\folder\\..\\Bodycam", root);
    expectInvalidDataRoot(env, L"", root);

    const auto blocked = root / L"blocked-data";
    std::ofstream(blocked) << "existing file";
    expectInvalidDataRoot(env, blocked.c_str(), root);
    check(std::filesystem::is_regular_file(blocked),
          "configured root never overwrites an existing file");

    // A redirection or file at Sessions must fail, never fall back to C:.
    env.set(selected.c_str());
    std::filesystem::remove_all(selected / L"Sessions", ec);
    std::ofstream(selected / L"Sessions") << "existing file";
    check(writableSessionRoot(root, true, &error).empty(),
          "file blocking configured Sessions is rejected");
    check(std::filesystem::is_regular_file(selected / L"Sessions"),
          "blocked session file preserved");

    // Junction/symlink checks are conditional because Windows may require
    // a developer-mode permission to create a test directory symlink.
    const auto linked = root / L"linked-data";
    if (CreateSymbolicLinkW(
            linked.c_str(), selected.c_str(),
            SYMBOLIC_LINK_FLAG_DIRECTORY |
                SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        expectInvalidDataRoot(env, linked.c_str(), root);
        std::filesystem::remove(linked, ec);
    }

    env.set(nullptr);
    std::filesystem::remove_all(root, ec);
    std::cout << "App storage tests passed\n";
}
