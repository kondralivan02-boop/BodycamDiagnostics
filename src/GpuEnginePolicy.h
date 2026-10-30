#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace telemetry {
inline wchar_t asciiLower(wchar_t value) noexcept {
    return value >= L'A' && value <= L'Z'
        ? static_cast<wchar_t>(value - L'A' + L'a')
        : value;
}
inline std::size_t findInsensitive(std::wstring_view text, std::wstring_view token,
                                   std::size_t start = 0) noexcept {
    if (token.empty() || token.size() > text.size() || start > text.size() - token.size())
        return std::wstring_view::npos;
    for (std::size_t i = start; i <= text.size() - token.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < token.size(); ++j) {
            if (asciiLower(text[i + j]) != asciiLower(token[j])) {
                match = false;
                break;
            }
        }
        if (match) return i;
    }
    return std::wstring_view::npos;
}
inline bool containsInsensitive(std::wstring_view text, std::wstring_view token) noexcept {
    return findInsensitive(text, token) != std::wstring_view::npos;
}

// Windows GPU Engine instance names carry the owning PID and engine type.
// Match a complete PID token and only the 3D engine; never sum unrelated PIDs.
inline bool isTargetProcess3DEngine(std::wstring_view counterPath,
                                    std::uint32_t processId) {
    const std::wstring pidToken = L"pid_" + std::to_wstring(processId);
    std::size_t pos = 0;
    while ((pos = findInsensitive(counterPath, pidToken, pos)) != std::wstring_view::npos) {
        const auto after = pos + pidToken.size();
        if (after == counterPath.size() || counterPath[after] == L'_' ||
            counterPath[after] == L')') {
            return containsInsensitive(counterPath, L"engtype_3d");
        }
        pos = after;
    }
    return false;
}
} // namespace telemetry
