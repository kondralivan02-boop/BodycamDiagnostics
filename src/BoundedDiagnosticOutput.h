#pragma once
#include <algorithm>
#include <array>
#include <string>
#include <string_view>

// Retain a prefix while the caller keeps draining all producer output. No
// allocation or growth in append(), even for an arbitrarily noisy child.
class BoundedDiagnosticOutput {
public:
    static constexpr std::size_t kCapacity = 16 * 1024;
    void append(std::string_view bytes) noexcept {
        const auto count = (std::min)(bytes.size(), kCapacity - size_);
        std::copy_n(bytes.data(), count, data_.data() + size_);
        size_ += count;
        truncated_ = truncated_ || count != bytes.size();
    }
    void clear() noexcept { size_ = 0; truncated_ = false; }
    std::size_t size() const noexcept { return size_; }
    bool truncated() const noexcept { return truncated_; }
    std::string text(std::size_t limit = kCapacity) const {
        const auto count = (std::min)(size_, limit);
        std::string result(data_.data(), count);
        for (char& c : result) {
            const auto byte = static_cast<unsigned char>(c);
            if (byte < 32 || byte == 127) c = ' ';
        }
        if (truncated_ || count < size_) result += " [stderr truncated]";
        return result;
    }
private:
    std::array<char, kCapacity> data_{};
    std::size_t size_ = 0;
    bool truncated_ = false;
};
