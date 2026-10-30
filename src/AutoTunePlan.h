#pragma once
#include "AutoTuneGpuPolicy.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace autotune {
enum class Preset { Performance, Balanced, Quality };
struct Hardware {
    std::uint64_t vramMiB = 0;
    std::uint64_t ramMiB = 0;
    unsigned width = 0;
    unsigned height = 0;
    std::uint64_t sharedGpuMemoryMiB = 0;
    unsigned logicalProcessors = 0;
    unsigned gpuAdapterCount = 0;
    std::uint32_t gpuVendorId = 0;
    std::uint32_t gpuDeviceId = 0;
    bool gpuSelectionAmbiguous = false;
    bool sharedOnlyGpu = false;
    bool gpuSelectedByHighPerformancePreference = false;
    std::string gpuName;
    std::string cpuName;
    unsigned physicalCores = 0;
    enum class ResolutionSource { GameIniLastConfirmed, GameIniCurrent, DesktopFallback };
    ResolutionSource resolutionSource = ResolutionSource::DesktopFallback;
    Hardware() = default;
    Hardware(std::uint64_t dedicated, std::uint64_t systemRam,
             unsigned displayWidth, unsigned displayHeight) noexcept
        : vramMiB(dedicated), ramMiB(systemRam), width(displayWidth), height(displayHeight) {}
};
struct Change { std::string key; int before = 0, after = 0; };
struct Plan { bool supported = false; std::string reason, updated; std::vector<Change> changes; };

// An initial hardware heuristic, not a measured optimum. Unknown memory fails closed.
// Conservative, vendor-neutral starting profile. VRAM is a capacity signal,
// not a GPU performance benchmark; CPU/RAM/resolution only cap the baseline.
// The selected adapter must be unambiguous before any INI change is proposed.
inline bool suggest(const Hardware& h, Preset& p) noexcept {
    if (!h.ramMiB || !h.width || !h.height || h.gpuSelectionAmbiguous) return false;
    if (!h.vramMiB) {
        if (!h.sharedGpuMemoryMiB) return false;
        p = Preset::Performance;
        return true;
    }
    const auto vram = nominalVramMiB(h.vramMiB);
    const auto pixels = std::uint64_t(h.width) * std::uint64_t(h.height);
    constexpr std::uint64_t k1080p = 1920ULL * 1080ULL;
    constexpr std::uint64_t k1440p = 2560ULL * 1440ULL;
    constexpr std::uint64_t k4k = 3840ULL * 2160ULL;
    if (h.ramMiB < 12ULL * 1024ULL ||
        (h.logicalProcessors != 0 && h.logicalProcessors < 4)) {
        p = Preset::Performance;
        return true;
    }
    if (pixels > k4k) {
        p = vram >= 24ULL * 1024ULL ? Preset::Balanced : Preset::Performance;
    } else if (pixels > k1440p) {
        p = vram >= 12ULL * 1024ULL ? Preset::Balanced : Preset::Performance;
    } else if (pixels > k1080p) {
        p = vram >= 8ULL * 1024ULL ? Preset::Balanced : Preset::Performance;
    } else {
        p = vram >= 6ULL * 1024ULL ? Preset::Balanced : Preset::Performance;
    }
    if (p == Preset::Balanced &&
        (h.ramMiB < 16ULL * 1024ULL ||
         (h.logicalProcessors != 0 && h.logicalProcessors < 6))) {
        p = Preset::Performance;
    }
    if (p == Preset::Balanced && pixels <= k1080p &&
        vram >= 16ULL * 1024ULL && h.ramMiB >= 32ULL * 1024ULL &&
        h.logicalProcessors >= 8) {
        p = Preset::Quality;
    }
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

struct GameResolution {
    unsigned width = 0;
    unsigned height = 0;
    Hardware::ResolutionSource source = Hardware::ResolutionSource::DesktopFallback;
    bool fromGameIni = false;
};

inline bool parseResolutionDimension(std::string_view text, unsigned& value) noexcept {
    text = trim(text);
    if (text.empty() || text.size() > 5) return false;
    unsigned parsed = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        parsed = parsed * 10U + static_cast<unsigned>(c - '0');
    }
    if (parsed == 0 || parsed > 16384) return false;
    value = parsed;
    return true;
}

// Reads only persisted game-resolution pairs. Duplicate, partial or malformed
// values are not guessed; the caller's desktop dimensions remain the fallback.
// This is configured output resolution, not a claim about internal render scale.
inline GameResolution resolveGameResolution(std::string_view ini,
                                             unsigned desktopWidth,
                                             unsigned desktopHeight) {
    struct Value {
        unsigned count = 0;
        unsigned parsed = 0;
        bool valid = true;
    };
    Value lastX, lastY, currentX, currentY;
    for (std::size_t pos = 0; pos < ini.size();) {
        const auto end = ini.find('\n', pos);
        const auto limit = end == std::string_view::npos ? ini.size() : end;
        auto line = trim(ini.substr(pos, limit - pos));
        if (pos == 0 && line.starts_with("\xef\xbb\xbf")) line.remove_prefix(3);
        if (!line.empty() && line.front() != ';' && line.front() != '#') {
            const auto eq = line.find('=');
            if (eq != std::string_view::npos) {
                const auto key = lower(trim(line.substr(0, eq)));
                Value* target = nullptr;
                if (key == "lastuserconfirmedresolutionsizex") target = &lastX;
                else if (key == "lastuserconfirmedresolutionsizey") target = &lastY;
                else if (key == "resolutionsizex") target = &currentX;
                else if (key == "resolutionsizey") target = &currentY;
                if (target) {
                    ++target->count;
                    unsigned parsed = 0;
                    if (target->count != 1 || !parseResolutionDimension(line.substr(eq + 1), parsed)) {
                        target->valid = false;
                    } else {
                        target->parsed = parsed;
                    }
                }
            }
        }
        if (end == std::string_view::npos) break;
        pos = end + 1;
    }
    const auto complete = [](const Value& x, const Value& y) {
        return x.count == 1 && y.count == 1 && x.valid && y.valid;
    };
    const bool confirmedPresent = lastX.count != 0 || lastY.count != 0;
    const bool currentPresent = currentX.count != 0 || currentY.count != 0;
    if (confirmedPresent && complete(lastX, lastY)) {
        return {lastX.parsed, lastY.parsed,
                Hardware::ResolutionSource::GameIniLastConfirmed, true};
    }
    if (!confirmedPresent && currentPresent && complete(currentX, currentY)) {
        return {currentX.parsed, currentY.parsed,
                Hardware::ResolutionSource::GameIniCurrent, true};
    }
    return {desktopWidth, desktopHeight,
            Hardware::ResolutionSource::DesktopFallback, false};
}

inline const char* resolutionSourceName(Hardware::ResolutionSource source) noexcept {
    switch (source) {
    case Hardware::ResolutionSource::GameIniLastConfirmed: return "Game INI (last confirmed)";
    case Hardware::ResolutionSource::GameIniCurrent: return "Game INI (current)";
    case Hardware::ResolutionSource::DesktopFallback: return "Desktop fallback";
    }
    return "Unknown";
}

inline const char* gpuVendorName(std::uint32_t vendorId) noexcept {
    switch (vendorId) {
    case 0x10DE: return "NVIDIA";
    case 0x1002: return "AMD";
    case 0x8086: return "Intel";
    default: return "Other/unknown";
    }
}

enum class BaselineConfidence { Low, Medium, High };

// Confidence describes input completeness for the baseline only. It is not
// a calibrated FPS prediction or a benchmark score.
inline BaselineConfidence baselineConfidence(const Hardware& h) noexcept {
    if (!h.ramMiB || !h.width || !h.height || h.gpuSelectionAmbiguous ||
        (!h.vramMiB && !h.sharedGpuMemoryMiB) || h.sharedOnlyGpu) {
        return BaselineConfidence::Low;
    }
    if (h.resolutionSource == Hardware::ResolutionSource::DesktopFallback ||
        h.gpuSelectedByHighPerformancePreference || h.gpuName.empty() ||
        h.cpuName.empty() || !h.physicalCores || !h.logicalProcessors) {
        return BaselineConfidence::Medium;
    }
    return BaselineConfidence::High;
}

inline const char* confidenceName(BaselineConfidence confidence) noexcept {
    switch (confidence) {
    case BaselineConfidence::Low: return "Low";
    case BaselineConfidence::Medium: return "Medium";
    case BaselineConfidence::High: return "High";
    }
    return "Unknown";
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
// A restore must never feed an empty, truncated or unsupported saved file to
// the Windows replacement path. Existing original backups were only created
// after prepare() accepted at least four known scalability settings.
inline bool validOriginalBackup(std::string_view ini) {
    return prepare(ini, Preset::Balanced).supported;
}
} // namespace autotune
