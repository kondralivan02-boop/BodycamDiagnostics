#pragma once

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <string>

template <typename Stream>
class BasicAtomicOutputFile {
public:
    explicit BasicAtomicOutputFile(
        const std::filesystem::path& output,
        std::ios::openmode mode =
            std::ios::binary | std::ios::trunc)
        : output_(output),
          temporary_(output.wstring() + L".tmp") {

        std::error_code ec;
        std::filesystem::remove(temporary_, ec);
        stream_.open(temporary_, mode);
    }

    BasicAtomicOutputFile(const BasicAtomicOutputFile&) = delete;
    BasicAtomicOutputFile& operator=(const BasicAtomicOutputFile&) = delete;

    ~BasicAtomicOutputFile() {
        if (stream_.is_open())
            stream_.close();

        if (!committed_) {
            std::error_code ec;
            std::filesystem::remove(temporary_, ec);
        }
    }

    bool isOpen() const noexcept {
        return stream_.is_open();
    }

    Stream& stream() noexcept {
        return stream_;
    }

    bool commit(std::wstring* error = nullptr) {
        if (committed_)
            return true;

        stream_.flush();
        if (!stream_) {
            if (error)
                *error = L"Cannot flush temporary report file";
            return false;
        }

        stream_.close();
        if (!MoveFileExW(
                temporary_.c_str(),
                output_.c_str(),
                MOVEFILE_REPLACE_EXISTING |
                    MOVEFILE_WRITE_THROUGH)) {
            const DWORD code = GetLastError();
            if (error) {
                *error =
                    L"Cannot atomically publish report file: " +
                    std::to_wstring(code);
            }
            return false;
        }

        committed_ = true;
        return true;
    }

private:
    std::filesystem::path output_;
    std::filesystem::path temporary_;
    Stream stream_;
    bool committed_ = false;
};

using AtomicOutputFile =
    BasicAtomicOutputFile<std::ofstream>;
using AtomicWideOutputFile =
    BasicAtomicOutputFile<std::wofstream>;

inline bool atomicCopyFile(
    const std::filesystem::path& source,
    const std::filesystem::path& target,
    std::wstring* error = nullptr,
    bool allowMissingSource = false) {

    const auto temporary =
        std::filesystem::path(target.wstring() + L".tmp");

    std::error_code ec;
    std::filesystem::remove(temporary, ec);

    ec.clear();
    std::filesystem::copy_file(
        source,
        temporary,
        std::filesystem::copy_options::overwrite_existing,
        ec);

    if (ec) {
        const auto copyError = ec;
        std::filesystem::remove(temporary, ec);

        if (allowMissingSource &&
            copyError == std::errc::no_such_file_or_directory) {
            return true;
        }

        if (error) {
            *error =
                L"Cannot copy temporary evidence file: " +
                std::to_wstring(copyError.value());
        }
        return false;
    }

    if (!MoveFileExW(
            temporary.c_str(),
            target.c_str(),
            MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        if (error) {
            *error =
                L"Cannot atomically publish copied evidence: " +
                std::to_wstring(code);
        }

        std::filesystem::remove(temporary, ec);
        return false;
    }

    return true;
}
