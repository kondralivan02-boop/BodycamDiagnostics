#include "AppStorage.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    std::wstring error;
    const auto user = userDataDirectory(&error);
    check(!user.empty(), "current-user LocalAppData resolves without elevation");
    const auto root = std::filesystem::temp_directory_path() / (L"Bodycam-storage-test-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec; std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    check(writableSessionRoot(root, true, &error) == user / L"Sessions", "single-file data always remains in user profile");
    check(writableSessionRoot(root, false, &error) == root / L"Sessions", "ordinary build retains writable portable sessions");
    std::filesystem::remove_all(root / L"Sessions", ec);
    std::ofstream(root / L"Sessions") << "existing file must not be overwritten";
    check(writableSessionRoot(root, false, &error) == user / L"Sessions", "blocked sessions folder falls back to profile");
    check(std::filesystem::is_regular_file(root / L"Sessions"), "fallback preserves existing user file");
    std::filesystem::remove_all(root, ec);
    std::cout << "App storage tests passed\n";
}
