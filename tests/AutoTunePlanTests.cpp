#include "AutoTunePlan.h"
#include <stdexcept>
#include <string>
#include <iostream>
namespace {
void check(bool ok, const char* expression, int line) {
    if (!ok) throw std::runtime_error(
        std::string("AutoTune check failed at line ") +
        std::to_string(line) + ": " + expression);
}
}
#define CHECK(condition) check(static_cast<bool>(condition), #condition, __LINE__)

int main() {
    using namespace autotune;
    Preset p = Preset::Quality;
    CHECK(!suggest({}, p));
    CHECK(suggest({6144, 16384, 1920, 1080}, p) && p == Preset::Balanced);
    CHECK(suggest({8192, 32768, 2560, 1440}, p) && p == Preset::Balanced);
    // Windows/DXGI reported 7943 MiB on an approximately 8-GiB GPU.
    // A narrow 256-MiB band avoids misclassifying the 8-GiB class at 1080p/1440p.
    CHECK(suggest({7935, 32694, 1920, 1080}, p) && p == Preset::Balanced);
    CHECK(suggest({7936, 32694, 1920, 1080}, p) && p == Preset::Balanced);
    CHECK(suggest({7943, 32694, 1920, 1080}, p) && p == Preset::Balanced);
    CHECK(suggest({7943, 32694, 2560, 1440}, p) && p == Preset::Balanced);
    // The reservation tolerance does not override RAM or high-resolution guards.
    CHECK(suggest({7943, 8192, 1920, 1080}, p) && p == Preset::Performance);
    CHECK(suggest({7943, 32694, 3840, 2160}, p) && p == Preset::Performance);
    CHECK(suggest({16384, 32768, 1920, 1080}, p) && p == Preset::Balanced);
    CHECK(suggest({12288, 32768, 3840, 2160}, p) && p == Preset::Balanced);
    Hardware quality{16384, 32768, 1920, 1080}; quality.logicalProcessors = 8;
    CHECK(suggest(quality, p) && p == Preset::Quality);
    const std::string original = "; comment\r\n[Graphics]\r\nSpecial=keep\r\n"
        "[ScalabilityGroups]\r\nsg.ShadowQuality=3\r\nsg.TextureQuality=3\r\n"
        "sg.PostProcessQuality=2\r\nsg.EffectsQuality=3\r\nUnknown=keep\r\n"
        "[Other]\r\nsg.ShadowQuality=99\r\n";
    auto r = prepare(original, Preset::Performance);
    CHECK(r.supported && r.changes.size() == 4);
    CHECK(r.updated.find("sg.ShadowQuality=1\r\n") != std::string::npos);
    CHECK(r.updated.find("Unknown=keep\r\n") != std::string::npos);
    CHECK(r.updated.find("[Other]\r\nsg.ShadowQuality=99\r\n") != std::string::npos);
    CHECK(prepare(r.updated, Preset::Performance).changes.empty());
    CHECK(!prepare("[ScalabilityGroups]\nsg.ShadowQuality=3\n", Preset::Balanced).supported);
    CHECK(!prepare("[ScalabilityGroups]\nsg.ShadowQuality=3\nsg.TextureQuality=2\nsg.PostProcessQuality=2\nsg.EffectsQuality=3\nsg.ShadowQuality=1\n", Preset::Balanced).supported);
    CHECK(!prepare("[ScalabilityGroups]\nsg.ShadowQuality=3\nsg.TextureQuality=2\nsg.PostProcessQuality=2\nsg.EffectsQuality=3\n[ScalabilityGroups]\n", Preset::Balanced).supported);
    CHECK(!prepare("[ScalabilityGroups]\nsg.ShadowQuality=3\nsg.TextureQuality=2\nsg.PostProcessQuality=none\nsg.EffectsQuality=3\n", Preset::Balanced).supported);
    CHECK(!prepare(std::string("xx\0yy", 5), Preset::Balanced).supported);
    CHECK(!prepare("[ScalabilityGroups]\nsg.ShadowQuality=3\nsg.TextureQuality=2\nsg.PostProcessQuality=99\nsg.EffectsQuality=3\n", Preset::Balanced).supported);
    const std::string bom = "\xef\xbb\xbf[ScalabilityGroups]\r\nsg.ShadowQuality=3\r\nsg.TextureQuality=3\r\nsg.PostProcessQuality=2\r\nsg.EffectsQuality=3\r\n";
    auto withBom = prepare(bom, Preset::Performance);
    CHECK(withBom.supported && withBom.updated.starts_with("\xef\xbb\xbf"));
    // A malformed section boundary must never cause edits in another section.
    const std::string withTrailingComment =
        "[ScalabilityGroups]\nsg.ShadowQuality=3\n"
        "[Other] ; user note\nsg.TextureQuality=3\n"
        "sg.PostProcessQuality=2\nsg.EffectsQuality=3\n";
    const auto badHeader = prepare(withTrailingComment, Preset::Performance);
    CHECK(!badHeader.supported);
    CHECK(badHeader.updated.empty() && badHeader.changes.empty());
    const auto unterminatedHeader = prepare(
        "[ScalabilityGroups]\nsg.ShadowQuality=3\n"
        "[Other\nsg.TextureQuality=3\nsg.PostProcessQuality=2\n"
        "sg.EffectsQuality=3\n", Preset::Balanced);
    CHECK(!unterminatedHeader.supported);
    CHECK(unterminatedHeader.updated.empty());
    // Failed parsing after earlier tentative edits must return no partial plan.
    const auto lateFailure = prepare(
        "[ScalabilityGroups]\nsg.ShadowQuality=3\n"
        "sg.TextureQuality=3\nsg.PostProcessQuality=3\n"
        "sg.EffectsQuality=not-a-number\n", Preset::Performance);
    CHECK(!lateFailure.supported);
    CHECK(lateFailure.updated.empty() && lateFailure.changes.empty());
    // Valid headers still switch section and preserve unrelated values.
    const auto validSections = prepare(
        "[ScalabilityGroups]\nsg.ShadowQuality=3\n"
        "sg.TextureQuality=3\nsg.PostProcessQuality=3\n"
        "sg.EffectsQuality=3\n[Other]\nsg.ShadowQuality=99\n",
        Preset::Performance);
    CHECK(validSections.supported);
    CHECK(validSections.updated.find("[Other]\nsg.ShadowQuality=99\n") != std::string::npos);
    // Quality must not silently downgrade Shadows from UE scalability 3 to 2.
    const std::string qualityIni =
        "[ScalabilityGroups]\nsg.ShadowQuality=3\nsg.TextureQuality=3\n"
        "sg.PostProcessQuality=3\nsg.EffectsQuality=3\n";
    const auto quality = prepare(qualityIni, Preset::Quality);
    CHECK(quality.supported);
    CHECK(quality.changes.empty());
    CHECK(quality.updated == qualityIni);
    // The actual Bodycam setting sg.ResolutionQuality is not rewritten
    // until game-version semantics and user monitor data are confirmed.
    const auto unknown = prepare(
        "[ScalabilityGroups]\nsg.ResolutionQuality=0\nsg.ShadowQuality=3\n"
        "sg.TextureQuality=3\nsg.PostProcessQuality=3\nsg.EffectsQuality=3\n"
        "[/Script/Bodycam.CustomGameUserSettings]\nUseSystemsConfig=True\n",
        Preset::Performance);
    CHECK(unknown.supported);
    CHECK(unknown.updated.find("sg.ResolutionQuality=0") != std::string::npos);
    CHECK(unknown.updated.find("UseSystemsConfig=True") != std::string::npos);
    std::cout << "AutoTunePlanTests PASS\n";
}
