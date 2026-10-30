#include "AutoTunePlan.h"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok, const char* what, int line) {
    if (!ok) throw std::runtime_error(std::string("AutoTune backup check failed at ") +
                                      std::to_string(line) + ": " + what);
}
}
#define CHECK(expr) check(static_cast<bool>(expr), #expr, __LINE__)

int main() {
    using autotune::validOriginalBackup;
    const std::string original =
        "; user comment\r\n[ScalabilityGroups]\r\nsg.ShadowQuality=3\r\n"
        "sg.TextureQuality=3\r\nsg.PostProcessQuality=2\r\n"
        "sg.EffectsQuality=3\r\n[Other]\r\nUnrelated=keep\r\n";
    CHECK(validOriginalBackup(original));
    CHECK(!validOriginalBackup(""));
    CHECK(!validOriginalBackup("[ScalabilityGroups]\nsg.ShadowQuality=3\n"));
    CHECK(!validOriginalBackup("[ScalabilityGroups]\nsg.ShadowQuality=3\n"
                               "sg.TextureQuality=3\nsg.PostProcessQuality=2\n"
                               "sg.EffectsQuality=99\n"));
    CHECK(!validOriginalBackup("[ScalabilityGroups]\nsg.ShadowQuality=3\n"
                               "sg.TextureQuality=3\nsg.PostProcessQuality=2\n"
                               "sg.EffectsQuality=3\n[Broken\n"));
    CHECK(!validOriginalBackup("[ScalabilityGroups]\nsg.ShadowQuality=3\n"
                               "sg.TextureQuality=3\nsg.PostProcessQuality=2\n"
                               "sg.EffectsQuality=3\nsg.EffectsQuality=1\n"));
    CHECK(!validOriginalBackup(std::string("\xff\xfe", 2)));
    CHECK(!validOriginalBackup(original + std::string(1, '\0')));
    CHECK(!validOriginalBackup(std::string(1024 * 1024 + 1, 'x')));
    std::cout << "AutoTuneBackupTests PASS\n";
}
