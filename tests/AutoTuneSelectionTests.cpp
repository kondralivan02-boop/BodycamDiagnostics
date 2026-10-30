#include "AutoTunePlan.h"
#include <iostream>
#include <stdexcept>
#include <string>
namespace {
void check(bool pass, const char* expression, int line) {
    if (!pass) throw std::runtime_error(std::string("AutoTune selection test line ") +
        std::to_string(line) + ": " + expression);
}
}
#define CHECK(expr) check(static_cast<bool>(expr), #expr, __LINE__)
using autotune::Hardware;
static Hardware hardware(std::uint64_t vram, std::uint64_t ram, unsigned width,
                         unsigned height, unsigned threads = 0) {
    Hardware h;
    h.vramMiB = vram; h.ramMiB = ram; h.width = width; h.height = height;
    h.logicalProcessors = threads;
    return h;
}
int main() {
    using namespace autotune;
    Preset p = Preset::Quality;
    CHECK(!suggest({}, p));
    auto ambiguousGpu = hardware(8192, 32768, 1920, 1080, 8);
    ambiguousGpu.gpuSelectionAmbiguous = true;
    CHECK(!suggest(ambiguousGpu, p));
    for (const auto gib : {2, 3, 4, 5}) {
        Hardware h = hardware(static_cast<std::uint64_t>(gib) * 1024, 32768, 1920, 1080, 8);
        CHECK(suggest(h, p) && p == Preset::Performance);
    }
    for (const auto gib : {6, 8, 10, 12}) {
        Hardware h = hardware(static_cast<std::uint64_t>(gib) * 1024, 32768, 1920, 1080, 8);
        CHECK(suggest(h, p) && p == Preset::Balanced);
    }
    for (const auto gib : {16, 20, 24, 32}) {
        Hardware h = hardware(static_cast<std::uint64_t>(gib) * 1024, 32768, 1920, 1080, 8);
        CHECK(suggest(h, p) && p == Preset::Quality);
    }
    for (const auto gib : {2, 4, 6}) {
        Hardware h = hardware(static_cast<std::uint64_t>(gib) * 1024, 32768, 2560, 1440, 8);
        CHECK(suggest(h, p) && p == Preset::Performance);
    }
    for (const auto gib : {8, 10, 12, 16, 24}) {
        Hardware h = hardware(static_cast<std::uint64_t>(gib) * 1024, 32768, 2560, 1440, 8);
        CHECK(suggest(h, p) && p == Preset::Balanced);
    }
    for (const auto gib : {2, 4, 6, 8, 10}) {
        Hardware h = hardware(static_cast<std::uint64_t>(gib) * 1024, 32768, 3840, 2160, 8);
        CHECK(suggest(h, p) && p == Preset::Performance);
    }
    for (const auto gib : {12, 16, 24, 32}) {
        Hardware h = hardware(static_cast<std::uint64_t>(gib) * 1024, 32768, 3840, 2160, 8);
        CHECK(suggest(h, p) && p == Preset::Balanced);
    }
    Hardware lowRam = hardware(24576, 8192, 1920, 1080, 16);
    CHECK(suggest(lowRam, p) && p == Preset::Performance);
    Hardware lowCpu = hardware(24576, 32768, 1920, 1080, 2);
    CHECK(suggest(lowCpu, p) && p == Preset::Performance);
    Hardware midCpu = hardware(24576, 32768, 1920, 1080, 6);
    CHECK(suggest(midCpu, p) && p == Preset::Balanced);
    Hardware unknownCpu = hardware(24576, 32768, 1920, 1080);
    CHECK(suggest(unknownCpu, p) && p == Preset::Balanced);
    Hardware uma = hardware(0, 32768, 1920, 1080, 8);
    uma.sharedGpuMemoryMiB = 16384; uma.sharedOnlyGpu = true;
    CHECK(suggest(uma, p) && p == Preset::Performance);
    Hardware unknownGpu = hardware(0, 32768, 1920, 1080);
    CHECK(!suggest(unknownGpu, p));
    Hardware observed = hardware(7943, 32694, 1920, 1080, 16);
    CHECK(suggest(observed, p) && p == Preset::Balanced);
    Hardware ultrawide = hardware(24576, 32768, 5120, 2880, 8);
    CHECK(suggest(ultrawide, p) && p == Preset::Balanced);
    Hardware ultrawideSmall = hardware(16384, 32768, 5120, 2880, 8);
    CHECK(suggest(ultrawideSmall, p) && p == Preset::Performance);
    const std::string original = "[ScalabilityGroups]\nsg.ShadowQuality=3\n"
        "sg.TextureQuality=3\nsg.PostProcessQuality=3\nsg.EffectsQuality=3\n";
    CHECK(validOriginalBackup(original));
    CHECK(!validOriginalBackup(""));
    std::cout << "AutoTuneSelectionTests PASS\n";
}
