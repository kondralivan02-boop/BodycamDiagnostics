#pragma once

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <string>

class AtomicOutputFile {
public:
    explicit AtomicOutputFile(const std::filesystem::path& output)
        : output_(output),
          temporary_(output.wstring() + L".tmp") {

        std::error_code ec;
        std::filesystem::remove(temporary_, ec);
        stream_.open(
            temporary_,
            std::ios::binary | std::ios::trunc);
    }

    AtomicOutputFile(const AtomicOutputFile&) = delete;
    AtomicOutputFile& operator=(const AtomicOutputFile&) = delete;

    ~AtomicOutputFile() {
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

    std::ofstream& stream() noexcept {
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
    std::ofstream stream_;
    bool committed_ = false;
};
