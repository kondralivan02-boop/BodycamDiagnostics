#include "AutoTuneGpuPolicy.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
void check(bool pass, const char* expression, int line) {
    if (!pass) throw std::runtime_error(std::string("GPU policy test line ") +
        std::to_string(line) + ": " + expression);
}
}
#define CHECK(expr) check(static_cast<bool>(expr), #expr, __LINE__)
int main() {
    using namespace autotune;
    for (const std::uint64_t gib : {2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32, 48}) {
        const auto nominal = gib * 1024ULL;
        const auto tolerance = (std::min)(nominal / 32ULL, 512ULL);
        CHECK(nominalVramMiB(nominal) == nominal);
        CHECK(nominalVramMiB(nominal - tolerance) == nominal);
        CHECK(nominalVramMiB(nominal - tolerance - 1) < nominal);
    }
    CHECK(nominalVramMiB(7943) == 8192);
    CHECK(nominalVramMiB(5960) == 6144);
    CHECK(nominalVramMiB(0) == 0);
    CHECK(nominalVramMiB(512) == 512);
    CHECK(nominalVramMiB(1024ULL * 1024 + 1) == 1024ULL * 1024 + 1);
    const auto single = selectGpuMemory(std::array{GpuAdapterMemory{7943, 16384, 0x10DE, 0x2684, false}}, false);
    CHECK(single.supported && !single.ambiguous);
    CHECK(single.dedicatedMiB == 7943 && single.sharedMiB == 16384);
    CHECK(single.vendorId == 0x10DE && single.deviceId == 0x2684);
    CHECK(single.adapterCount == 1 && single.selectedIndex == 0);
    const auto integrated = selectGpuMemory(std::array{GpuAdapterMemory{0, 8192, 0x8086, 0xA7A0, false}}, false);
    CHECK(integrated.supported && integrated.sharedOnly);
    CHECK(integrated.dedicatedMiB == 0 && integrated.sharedMiB == 8192);
    CHECK(!selectGpuMemory(std::array{GpuAdapterMemory{0, 0, 0, 0, false}}, false).supported);
    CHECK(!selectGpuMemory(std::vector<GpuAdapterMemory>{}, false).supported);
    CHECK(selectGpuMemory(std::array{GpuAdapterMemory{0, 0, 0, 0, true}, GpuAdapterMemory{12288, 8192, 0x1002, 0x744C, false}}, false).supported);
    const auto ambiguous = selectGpuMemory(std::array{GpuAdapterMemory{0, 8192, 0x8086, 0xA7A0, false}, GpuAdapterMemory{8192, 8192, 0x10DE, 0x2684, false}}, false);
    CHECK(!ambiguous.supported && ambiguous.ambiguous && ambiguous.adapterCount == 2);
    const auto preferred = selectGpuMemory(std::array{GpuAdapterMemory{0, 0, 0, 0, true}, GpuAdapterMemory{8192, 8192, 0x10DE, 0x2684, false}, GpuAdapterMemory{0, 8192, 0x8086, 0xA7A0, false}}, true);
    CHECK(preferred.supported && !preferred.ambiguous);
    CHECK(preferred.highPerformancePreferenceUsed && preferred.adapterCount == 2);
    CHECK(preferred.selectedIndex == 1 && preferred.vendorId == 0x10DE);
    const auto preferredShared = selectGpuMemory(std::array{GpuAdapterMemory{0, 8192, 0x8086, 0xA7A0, false}, GpuAdapterMemory{8192, 8192, 0x10DE, 0x2684, false}}, true);
    CHECK(preferredShared.supported && preferredShared.sharedOnly);
    CHECK(preferredShared.highPerformancePreferenceUsed);
    std::cout << "AutoTuneGpuPolicyTests PASS\n";
}
