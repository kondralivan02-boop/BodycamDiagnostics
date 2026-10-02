#include "ToolPreflight.h"
#include <Windows.h>
#include <bcrypt.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <vector>

namespace {
std::wstring quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

std::wstring widenUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

bool runAndCapture(const std::filesystem::path& exe, const std::wstring& args,
                   std::string& output, DWORD timeoutMs = 5000) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return false;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    std::wstring command = quote(exe.wstring()) + L" " + args;
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(
        exe.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, exe.parent_path().c_str(), &si, &pi);

    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        return false;
    }

    std::string captured;
    char buffer[4096];
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;

    for (;;) {
        DWORD available = 0;
        if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            DWORD read = 0;
            const DWORD want = (std::min)(available, static_cast<DWORD>(sizeof(buffer)));
            if (ReadFile(readPipe, buffer, want, &read, nullptr) && read > 0)
                captured.append(buffer, buffer + read);
        }

        const DWORD wait = WaitForSingleObject(pi.hProcess, 50);
        if (wait == WAIT_OBJECT_0) break;
        if (GetTickCount64() >= deadline) {
            TerminateProcess(pi.hProcess, 3);
            WaitForSingleObject(pi.hProcess, 500);
            break;
        }
    }

    for (;;) {
        DWORD read = 0;
        if (!ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
        captured.append(buffer, buffer + read);
    }

    CloseHandle(readPipe);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    output = std::move(captured);
    return true;
}

bool containsAscii(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool isSha256Hex(const std::string& value) {
    if (value.size() != 64)
        return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}
}

std::string fileSha256Hex(const std::filesystem::path& file) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectBytes = 0;
    DWORD hashBytes = 0;
    DWORD cb = 0;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return {};

    auto closeAlg = [&]() { if (alg) BCryptCloseAlgorithmProvider(alg, 0); };
    auto destroyHash = [&]() { if (hash) BCryptDestroyHash(hash); };

    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes), &cb, 0) < 0 ||
        BCryptGetProperty(alg, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashBytes), sizeof(hashBytes), &cb, 0) < 0) {
        closeAlg();
        return {};
    }

    std::vector<UCHAR> object(objectBytes);
    std::vector<UCHAR> digest(hashBytes);
    if (BCryptCreateHash(alg, &hash, object.data(),
            static_cast<ULONG>(object.size()), nullptr, 0, 0) < 0) {
        closeAlg();
        return {};
    }

    std::ifstream in(file, std::ios::binary);
    if (!in) {
        destroyHash();
        closeAlg();
        return {};
    }

    std::vector<char> buffer(1024 * 1024);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto got = in.gcount();
        if (got > 0 && BCryptHashData(hash,
                reinterpret_cast<PUCHAR>(buffer.data()),
                static_cast<ULONG>(got), 0) < 0) {
            destroyHash();
            closeAlg();
            return {};
        }
    }

    if (!in.eof() || BCryptFinishHash(hash, digest.data(),
            static_cast<ULONG>(digest.size()), 0) < 0) {
        destroyHash();
        closeAlg();
        return {};
    }

    destroyHash();
    closeAlg();

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : digest)
        out << std::setw(2) << static_cast<unsigned>(byte);
    return out.str();
}

std::string expectedRuntimeToolSha256(
    const std::filesystem::path& manifest,
    const std::filesystem::path& fileName) {

    std::ifstream in(manifest, std::ios::binary);
    if (!in)
        return {};

    const auto wanted = lowerAscii(fileName.filename().string());
    std::string matched;
    std::string line;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#')
            continue;

        std::istringstream fields(line);
        std::string hash;
        std::string name;
        if (!(fields >> hash >> name))
            continue;

        hash = lowerAscii(hash);
        if (!isSha256Hex(hash) || lowerAscii(name) != wanted)
            continue;

        if (!matched.empty() && matched != hash)
            return {};

        matched = std::move(hash);
    }

    return matched;
}

std::filesystem::path resolveRuntimeTool(
    const std::filesystem::path& appDirectory,
    const std::filesystem::path& fileName) {

    const auto preferred = appDirectory / L"Tools" / fileName;
    std::error_code ec;
    if (std::filesystem::is_regular_file(preferred, ec))
        return preferred;

    ec.clear();
    const auto fallback = appDirectory / fileName;
    if (std::filesystem::is_regular_file(fallback, ec))
        return fallback;

    return preferred;
}

ToolPreflightResult runToolPreflight(
    const std::filesystem::path& presentMonExe,
    const std::filesystem::path& ffmpegExe,
    const std::filesystem::path& hashManifest) {

    ToolPreflightResult r;
    std::error_code ec;
    r.presentMonFound = std::filesystem::is_regular_file(presentMonExe, ec);
    ec.clear();
    r.ffmpegFound = std::filesystem::is_regular_file(ffmpegExe, ec);
    ec.clear();
    r.hashManifestFound = std::filesystem::is_regular_file(hashManifest, ec);

    const auto expectedPresentMon =
        expectedRuntimeToolSha256(hashManifest, presentMonExe.filename());
    const auto expectedFfmpeg =
        expectedRuntimeToolSha256(hashManifest, ffmpegExe.filename());

    if (r.presentMonFound && !expectedPresentMon.empty())
        r.presentMonTrusted = fileSha256Hex(presentMonExe) == expectedPresentMon;
    if (r.ffmpegFound && !expectedFfmpeg.empty())
        r.ffmpegTrusted = fileSha256Hex(ffmpegExe) == expectedFfmpeg;

    std::wstring d;
    d += L"Runtime hash manifest: ";
    d += r.hashManifestFound ? L"OK" : L"MISSING";
    d += L"\n";
    d += L"PresentMon: ";
    d += r.presentMonFound ? (r.presentMonTrusted ? L"OK (verified)" : L"FOUND (hash mismatch)") : L"MISSING";
    d += L"\nFFmpeg: ";
    d += r.ffmpegFound ? (r.ffmpegTrusted ? L"OK (verified)" : L"FOUND (hash mismatch)") : L"MISSING";

    if (r.ffmpegFound && r.ffmpegTrusted) {
        std::string encoders;
        if (runAndCapture(ffmpegExe, L"-hide_banner -encoders", encoders))
            r.nvencAvailable = containsAscii(encoders, "h264_nvenc");

        std::string filters;
        if (runAndCapture(ffmpegExe, L"-hide_banner -filters", filters))
            r.ddagrabAvailable = containsAscii(filters, "ddagrab");

        d += L"\nNVENC (h264_nvenc): ";
        d += r.nvencAvailable ? L"OK" : L"NOT FOUND";
        d += L"\nDesktop Duplication (ddagrab): ";
        d += r.ddagrabAvailable ? L"OK" : L"NOT FOUND";
    }

    r.details = std::move(d);
    return r;
}
