#include "BodycamLogs.h"
#include <fstream>
#ifdef _WIN32
#include <Windows.h>
#endif
#include <iostream>
#include <stdexcept>
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    const auto root = std::filesystem::temp_directory_path() / ("bodycam-log-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto source = root / "source";
    const auto output = root / "session";
    std::filesystem::create_directories(output);
    const auto now = std::chrono::system_clock::now();
    check(collectBodycamLogs({}, output, now).source.status == "not_configured", "unresolved path");
    check(collectBodycamLogs(source, output, now).source.status == "not_found", "missing logs optional");
    std::filesystem::create_directories(source);
    check(collectBodycamLogs(source, output, now).source.status == "available_empty", "empty is readable");
    std::ofstream(source / "old.log") << "old";
    std::filesystem::last_write_time(source / "old.log", std::filesystem::file_time_type::clock::now() - std::chrono::hours(1));
    std::ofstream(source / "ignore.bin") << "binary";
    std::ofstream(source / "Bodycam.log") << "human-readable log\n";
#ifdef _WIN32
    HANDLE locked = CreateFileW((source / "Bodycam.log").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    check(locked != INVALID_HANDLE_VALUE, "open exclusive test fixture");
    const auto blocked = collectBodycamLogs(source, output, now);
    CloseHandle(locked);
    check(blocked.files.empty() && blocked.source.status == "error" && blocked.source.errorCode == ERROR_SHARING_VIOLATION, "retain native read failure with no evidence");
#endif
    const auto one = collectBodycamLogs(source, output, now);
    check(one.files.size() == 1, "only recent log files");
    check(one.source.status == "available", "successful evidence collection");
    check(std::filesystem::exists(output / "bodycam_logs" / "Bodycam.log"), "report artifact copied");
    {
        std::ofstream large(source / "large.log", std::ios::binary);
        std::string bytes(2 * 1024 * 1024 + 10, 'x');
        bytes.replace(bytes.size() - 4, 4, "TAIL"); large << bytes;
    }
    const auto capped = collectBodycamLogs(source, output, now);
    check(capped.source.status == "partial", "truncation is explicit");
    check(writeBodycamLogsManifest(capped, output / "bodycam_logs_manifest.json"), "manifest written");
    std::ifstream manifest(output / "bodycam_logs_manifest.json");
    const std::string manifestText((std::istreambuf_iterator<char>(manifest)), std::istreambuf_iterator<char>());
    manifest.close();
    check(manifestText.find("\"tail_only\":true") != std::string::npos, "manifest identifies partial file evidence");
    check(!writeBodycamLogsManifest(capped, root / "missing" / "manifest.json"), "manifest write failure reported");
    check(std::filesystem::file_size(output / "bodycam_logs" / "large.log") == 2 * 1024 * 1024, "per-file cap");
    std::ifstream tail(output / "bodycam_logs" / "large.log", std::ios::binary);
    tail.seekg(-4, std::ios::end); char end[4]{}; tail.read(end, 4);
    check(std::string(end, 4) == "TAIL", "retain newest tail");
    tail.close();
    for (int i = 0; i < 12; ++i) std::ofstream(source / (std::to_string(i) + ".log")) << "entry";
    const auto many = collectBodycamLogs(source, output, now);
    check(many.files.size() <= 8 && many.bytesCopied <= 8 * 1024 * 1024, "count and total byte bounds");
    check(many.source.status == "partial", "collection cap is visible");
    std::filesystem::remove_all(root);
    std::cout << "Bodycam logs tests passed\n";
}
