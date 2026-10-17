#pragma once
#include <array>
#include <cstddef>
#include <istream>
#include <string>
#include <utility>

namespace autotune {
inline constexpr std::size_t kMaxGameIniBytes = 1024 * 1024;

// A stat-based size check is not enough: the file might grow while it is
// being read. Cap actual bytes and never expose partial contents on error.
inline bool readBounded(std::istream& input, std::string& output,
                        std::size_t maxBytes = kMaxGameIniBytes) {
    if (!input) return false;
    std::string candidate;
    std::array<char, 4096> buffer{};
    for (;;) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count < 0 || static_cast<std::size_t>(count) > maxBytes - candidate.size())
            return false;
        candidate.append(buffer.data(), static_cast<std::size_t>(count));
        if (input.bad()) return false;
        if (input.eof()) {
            output = std::move(candidate);
            return true;
        }
        if (!input) return false;
    }
}
} // namespace autotune
