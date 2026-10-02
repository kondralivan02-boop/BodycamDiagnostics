#pragma once

#include <filesystem>
#include <fstream>
#include <string>

class AtomicOutputFile {
public:
    explicit AtomicOutputFile(const std::filesystem::path& output)
        : output_(output),
          stream_(output, std::ios::binary | std::ios::trunc) {}

    ~AtomicOutputFile() = default;

    bool isOpen() const noexcept {
        return stream_.is_open();
    }

    std::ofstream& stream() noexcept {
        return stream_;
    }

    bool commit(std::wstring* error = nullptr) {
        stream_.flush();
        if (!stream_) {
            if (error)
                *error = L"Cannot flush output file";
            return false;
        }
        return true;
    }

private:
    std::filesystem::path output_;
    std::ofstream stream_;
};
