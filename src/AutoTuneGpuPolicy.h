#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
namespace autotune {
inline constexpr std::uint64_t nominalVramMiB(std::uint64_t reportedMiB) noexcept {
    if (reportedMiB < 512 || reportedMiB > 1024ULL * 1024) return reportedMiB;
    const auto nominalGiB = (reportedMiB + 1023ULL) / 1024ULL;
    const auto nominalMiB = nominalGiB * 1024ULL;
    const auto toleranceMiB = (std::min)(nominalMiB / 32ULL, 512ULL);
    return nominalMiB >= reportedMiB && nominalMiB - reportedMiB <= toleranceMiB
        ? nominalMiB : reportedMiB;
}
struct GpuAdapterMemory {
    std::uint64_t dedicatedMiB = 0;
    std::uint64_t sharedMiB = 0;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    bool software = false;
};
struct GpuSelection {
    bool supported = false;
    bool ambiguous = false;
    bool sharedOnly = false;
    bool highPerformancePreferenceUsed = false;
    std::size_t adapterCount = 0;
    std::size_t selectedIndex = 0;
    std::uint64_t dedicatedMiB = 0;
    std::uint64_t sharedMiB = 0;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
};
inline GpuSelection selectGpuMemory(std::span<const GpuAdapterMemory> adapters,
                                    bool highPerformanceOrderKnown) noexcept {
    GpuSelection result;
    bool found = false;
    for (std::size_t i = 0; i < adapters.size(); ++i) {
        const auto& adapter = adapters[i];
        if (adapter.software) continue;
        ++result.adapterCount;
        if (!found) { found = true; result.selectedIndex = i; }
    }
    if (!found) return result;
    if (result.adapterCount > 1) {
        if (!highPerformanceOrderKnown) { result.ambiguous = true; return result; }
        result.highPerformancePreferenceUsed = true;
    }
    const auto& selected = adapters[result.selectedIndex];
    if (selected.dedicatedMiB == 0 && selected.sharedMiB == 0) {
        result.ambiguous = result.adapterCount > 1;
        return result;
    }
    result.dedicatedMiB = selected.dedicatedMiB;
    result.sharedMiB = selected.sharedMiB;
    result.vendorId = selected.vendorId;
    result.deviceId = selected.deviceId;
    result.sharedOnly = selected.dedicatedMiB == 0;
    result.supported = true;
    return result;
}
} // namespace autotune
