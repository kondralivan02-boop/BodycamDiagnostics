#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace autotune {
enum class Preset { Performance, Balanced, Quality };
struct Hardware { std::uint64_t vramMiB = 0, ramMiB = 0; unsigned width = 0, height = 0; };
struct Change { std::string key; int before = 0, after = 0; };
struct Plan { bool supported = false; std::string reason, updated; std::vector<Change> changes; };

// An initial hardware heuristic, not a measured optimum. Unknown memory fails closed.
inline bool suggest(const Hardware& h, Preset& p) noexcept {
    if (!h.vramMiB || !h.ramMiB || !h.width || !h.height) return false;
    const auto pixels = std::uint64_t(h.width) * h.height;
    // DXGI may expose slightly less than the physical 8-GiB VRAM capacity
    // (7943 MiB observed on Windows). Limit the allowance to 256 MiB, not
    // broad rounding of small GPUs into a higher performance class.
    constexpr std::uint64_t kEightGiBClassFloorMiB = 8192 - 256;
    if (h.vramMiB < kEightGiBClassFloorMiB || h.ramMiB < 16384 ||
        (pixels > 2560ULL * 1440 && h.vramMiB < 12288)) p = Preset::Performance;
    else if (h.vramMiB < 12288 || pixels > 2560ULL * 1440) p = Preset::Balanced;
    else p = Preset::Quality;
    return true;
}
inline const char* name(Preset p) noexcept {
    switch (p) { case Preset::Performance: return "Performance";
                 case Preset::Balanced: return "Balanced";
                 case Preset::Quality: return "Quality"; }
    return "Unknown";
}
inline std::string lower(std::string_view s) {
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}
inline std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}
struct Setting { const char* key; std::array<int, 3> values; };
inline constexpr std::array<Setting, 10> kKnownSettings{{
    {"sg.viewdistancequality", {2, 2, 3}},
    {"sg.antialiasingquality", {2, 2, 3}},
    {"sg.shadowquality", {1, 2, 3}},
    {"sg.globalilluminationquality", {1, 2, 3}},
    {"sg.reflectionquality", {1, 2, 3}},
    {"sg.postprocessquality", {1, 2, 3}},
    {"sg.texturequality", {2, 2, 3}},
    {"sg.effectsquality", {1, 2, 3}},
    {"sg.foliagequality", {1, 2, 3}},
    {"sg.shadingquality", {1, 2, 3}},
}};

// Modify existing, recognized scalability keys only. Do not create unknown
// graphics/DLSS keys, normalize line endings, or overwrite other sections.
inline Plan prepare(std::string_view ini, Preset preset) {
    Plan out;
    if (ini.empty() || ini.size() > 1024 * 1024 ||
        ini.find('\0') != std::string_view::npos ||
        (ini.size() >= 2 && ((unsigned char)ini[0] == 0xff || (unsigned char)ini[0] == 0xfe))) {
        out.reason = "Unsupported or oversized INI encoding"; return out;
    }
    const auto index = static_cast<std::size_t>(preset);
    if (index >= 3) { out.reason = "Unknown preset"; return out; }
    // Reject unsupported layouts without exposing a partially rewritten INI.
    const auto reject = [&out](const char* reason) -> Plan {
        out.supported = false;
        out.reason = reason;
        out.updated.clear();
        out.changes.clear();
        return out;
    };
    out.updated.reserve(ini.size());
    bool section = false, sawSection = false;
    std::array<bool, kKnownSettings.size()> seen{};
    std::size_t recognized = 0;
    for (std::size_t pos = 0; pos < ini.size();) {
        const auto end = ini.find('\n', pos);
        const auto limit = end == std::string_view::npos ? ini.size() : end;
        const auto raw = ini.substr(pos, limit - pos);
        auto line = trim(raw);
        if (pos == 0 && line.starts_with("\xef\xbb\xbf")) line.remove_prefix(3);
        // A malformed or annotated section header must not leave us writing
        // under the previous [ScalabilityGroups] section. Reject the entire
        // plan instead of guessing whether the game will parse that header.
        if (!line.empty() && line.front() == '[') {
            if (line.size() < 3 || line.back() != ']' ||
                line.find(']') != line.size() - 1) {
                return reject("Unsupported INI section header");
            }
            section = lower(line) == "[scalabilitygroups]";
            if (section && sawSection) return reject("Duplicate section");
            sawSection |= section;
        }
        bool changed = false;
        if (section && !line.empty() && line.front() != ';' && line.front() != '#') {
            const auto eq = raw.find('=');
            if (eq != std::string_view::npos) {
                const auto key = lower(trim(raw.substr(0, eq)));
                for (std::size_t n = 0; n < kKnownSettings.size(); ++n) {
                    if (key != kKnownSettings[n].key) continue;
                    if (seen[n]) { return reject("Duplicate key"); }
                    seen[n] = true; ++recognized;
                    const auto old = trim(raw.substr(eq + 1));
                    if (old.empty() || old.size() > 2 ||
                        !std::all_of(old.begin(), old.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })) {
                        return reject("Unsupported scalability value");
                    }
                    int original = 0;
                    for (char c : old) original = original * 10 + (c - '0');
                    if (original > 4) { return reject("Unsupported scalability level"); }
                    const int target = kKnownSettings[n].values[index];
                    if (original != target) {
                        out.updated.append(raw.substr(0, eq + 1));
                        const auto afterEq = raw.substr(eq + 1);
                        const auto leading = afterEq.find_first_not_of(" \t");
                        if (leading != std::string_view::npos) out.updated.append(afterEq.substr(0, leading));
                        out.updated += std::to_string(target);
                        const auto last = afterEq.find_last_not_of(" \t\r");
                        if (last != std::string_view::npos) out.updated.append(afterEq.substr(last + 1));
                        out.changes.push_back(Change{key, original, target});
                        changed = true;
                    }
                    break;
                }
            }
        }
        if (!changed) out.updated.append(raw);
        if (end != std::string_view::npos) out.updated.push_back('\n');
        pos = end == std::string_view::npos ? ini.size() : end + 1;
    }
    if (!sawSection || recognized < 4) {
        return reject("No compatible ScalabilityGroups section with >=4 known keys");
    }
    out.supported = true;
    return out;
}
} // namespace autotune
