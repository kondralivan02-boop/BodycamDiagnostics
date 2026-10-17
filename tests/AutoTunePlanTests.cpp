#include "AutoTunePlan.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace autotune;
    Preset p = Preset::Quality;
    assert(!suggest({}, p));
    assert(suggest({6144, 16384, 1920, 1080}, p) && p == Preset::Performance);
    assert(suggest({8192, 32768, 2560, 1440}, p) && p == Preset::Balanced);
    assert(suggest({16384, 32768, 1920, 1080}, p) && p == Preset::Quality);
    assert(suggest({12288, 32768, 3840, 2160}, p) && p == Preset::Balanced);
    const std::string original = "; comment\r\n[Graphics]\r\nSpecial=keep\r\n"
        "[ScalabilityGroups]\r\nsg.ShadowQuality=3\r\nsg.TextureQuality=3\r\n"
        "sg.PostProcessQuality=2\r\nsg.EffectsQuality=3\r\nUnknown=keep\r\n"
        "[Other]\r\nsg.ShadowQuality=99\r\n";
    auto r = prepare(original, Preset::Performance);
    assert(r.supported && r.changes.size() == 4);
    assert(r.updated.find("sg.ShadowQuality=1\r\n") != std::string::npos);
    assert(r.updated.find("Unknown=keep\r\n") != std::string::npos);
    assert(r.updated.find("[Other]\r\nsg.ShadowQuality=99\r\n") != std::string::npos);
    assert(prepare(r.updated, Preset::Performance).changes.empty());
    assert(!prepare("[ScalabilityGroups]\nsg.ShadowQuality=3\n", Preset::Balanced).supported);
    assert(!prepare("[ScalabilityGroups]\nsg.ShadowQuality=3\nsg.TextureQuality=2\nsg.PostProcessQuality=2\nsg.EffectsQuality=3\nsg.ShadowQuality=1\n", Preset::Balanced).supported);
    assert(!prepare("[ScalabilityGroups]\nsg.ShadowQuality=3\nsg.TextureQuality=2\nsg.PostProcessQuality=2\nsg.EffectsQuality=3\n[ScalabilityGroups]\n", Preset::Balanced).supported);
    assert(!prepare("[ScalabilityGroups]\nsg.ShadowQuality=3\nsg.TextureQuality=2\nsg.PostProcessQuality=none\nsg.EffectsQuality=3\n", Preset::Balanced).supported);
    assert(!prepare(std::string("xx\0yy", 5), Preset::Balanced).supported);
    assert(!prepare("[ScalabilityGroups]\nsg.ShadowQuality=3\nsg.TextureQuality=2\nsg.PostProcessQuality=99\nsg.EffectsQuality=3\n", Preset::Balanced).supported);
    const std::string bom = "\xef\xbb\xbf[ScalabilityGroups]\r\nsg.ShadowQuality=3\r\nsg.TextureQuality=3\r\nsg.PostProcessQuality=2\r\nsg.EffectsQuality=3\r\n";
    auto withBom = prepare(bom, Preset::Performance);
    assert(withBom.supported && withBom.updated.starts_with("\xef\xbb\xbf"));
    std::cout << "AutoTunePlanTests PASS\n";
}
