#include "BuildInfo.h"
#include "BuildInfoGenerated.h"

#include <Windows.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>

namespace {
std::string jsonEscape(std::string_view source) {
    std::string out;
    out.reserve(source.size() + 8);

    for (const unsigned char c : source) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
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

bool writeBuildInfoReport(
    const std::filesystem::path& path,
    std::wstring* error) {

    const auto temporary =
        std::filesystem::path(path.wstring() + L".tmp");

    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = L"Cannot create build_info.json";
        return false;
    }

    out << "{\n"
        << "  \"schema_version\": 1,\n"
        << "  \"commit_sha\": \"" << jsonEscape(bodycam::build::kCommitSha) << "\",\n"
        << "  \"branch\": \"" << jsonEscape(bodycam::build::kBranch) << "\",\n"
        << "  \"ci_run\": \"" << jsonEscape(bodycam::build::kCiRun) << "\"\n"
        << "}\n";

    out.flush();
    if (!out) {
        if (error) *error = L"Cannot write build_info.json";
        out.close();
        std::error_code cleanupEc;
        std::filesystem::remove(temporary, cleanupEc);
        return false;
    }
    out.close();

    if (!MoveFileExW(
            temporary.c_str(),
            path.c_str(),
            MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH)) {
        const DWORD publishError = GetLastError();
        if (error) {
            *error =
                L"Cannot publish build_info.json: " +
                std::to_wstring(publishError);
        }

        std::error_code cleanupEc;
        std::filesystem::remove(temporary, cleanupEc);
        return false;
    }

    return true;
}
