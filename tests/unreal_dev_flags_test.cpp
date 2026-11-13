// Terrain.Flags of the tools of the developers (agent 51, Bridge/dev_flags.hpp): its arguments
// ("none", words joined by commas, nothing: the state); what it tells, word for word; the flags it
// lays over those of the actor for the next Plays (a session and the planet job of Terrain.Seed),
// their planet file aside under Saved/TerrainEngine/dev, never the one of the user; nothing laid
// while it is not set; and the line of the start that says where the flags come from.

#include "dev_flags.hpp"

#include "test_assert.hpp"

#include <array>
#include <string>
#include <vector>

namespace {

using terrain::ExperimentalFlag;
using terrain::WorldParams;
using terrain::unreal::DevFlagsCommand;
using terrain::unreal::FlagLine;

constexpr std::array<ExperimentalFlag, 2> kTable = {{
    {"alpha", "test", [](WorldParams& p) { p.relief.maskLow = -0.2f; }},
    {"beta", "test", [](WorldParams& p) { p.ecology.biomes.snowTemperature = -2.0f; }},
}};

const std::string kBase = "/project";

void testParse() {
    using terrain::unreal::parseDevFlags;
    TEST_CHECK((parseDevFlags(std::vector<std::string>{}) == DevFlagsCommand{false, ""}));
    TEST_CHECK((parseDevFlags(std::vector<std::string>{"none"}) == DevFlagsCommand{true, ""}));
    TEST_CHECK((parseDevFlags(std::vector<std::string>{"seas"}) == DevFlagsCommand{true, "seas"}));
    TEST_CHECK((parseDevFlags(std::vector<std::string>{"seas", "volcanoes"}) ==
                DevFlagsCommand{true, "seas,volcanoes"}));
    TEST_CHECK(terrain::unreal::devFlagsPlanetPath("alpha,beta") ==
               "Saved/TerrainEngine/dev/planet_{Seed}_alpha+beta.bin");
}

terrain::unreal::SessionParams actor() {
    terrain::unreal::SessionParams params;
    params.seed = 7;
    params.experimental = "beta";
    params.planetFile = "/project/Saved/TerrainEngine/planet_7.bin";
    return params;
}

void testNotSet() {
    terrain::unreal::clearDevFlags();
    TEST_CHECK(!terrain::unreal::devFlags().has_value());
    terrain::unreal::SessionParams params = actor();
    TEST_CHECK(terrain::unreal::applyDevFlags(params, kBase, kTable).empty());
    TEST_CHECK(params.experimental == "beta" && params.planetFile == actor().planetFile);
    TEST_CHECK((terrain::unreal::devFlagsLines(kTable) ==
                std::vector<FlagLine>{
                    {false, "Terrain.Flags: not set: the next Plays take the setting of the "
                            "actor"}}));
}

void testSet() {
    terrain::unreal::setDevFlags("beta alpha");
    terrain::unreal::SessionParams params = actor();
    TEST_CHECK(terrain::unreal::applyDevFlags(params, kBase, kTable) == "Terrain.Flags");
    TEST_CHECK(params.experimental == "beta alpha");
    TEST_CHECK(params.planetFile == "/project/Saved/TerrainEngine/dev/planet_7_alpha+beta.bin");
    // No planet file configured: none for the flags either (the relief alone).
    terrain::unreal::SessionParams alone = actor();
    alone.planetFile.clear();
    (void)terrain::unreal::applyDevFlags(alone, kBase, kTable);
    TEST_CHECK(alone.planetFile.empty() && alone.experimental == "beta alpha");
    // The planet job of Terrain.Seed: the file aside, written there if need be.
    terrain::unreal::PlanetChoice choice{"/user/planet.bin", false, "/project/x.bin"};
    std::string experimental = "beta";
    terrain::unreal::applyDevFlags(choice, experimental, 9, kBase, kTable);
    TEST_CHECK(experimental == "beta alpha" && choice.mayWrite && choice.fallback.empty());
    TEST_CHECK(choice.file == "/project/Saved/TerrainEngine/dev/planet_9_alpha+beta.bin");
    TEST_CHECK((terrain::unreal::devFlagsLines(kTable) ==
                std::vector<FlagLine>{
                    {false, "Terrain.Flags: \"alpha,beta\" for the next Plays of this editor, over "
                            "the setting of the actor (planet file: "
                            "Saved/TerrainEngine/dev/planet_{Seed}_alpha+beta.bin)"}}));
}

// "none": no flag, the planet file of the actor; unknown names only: the same, with a warning.
void testNoneAndUnknown() {
    terrain::unreal::setDevFlags("");
    terrain::unreal::SessionParams params = actor();
    TEST_CHECK(terrain::unreal::applyDevFlags(params, kBase, kTable) == "Terrain.Flags");
    TEST_CHECK(params.experimental.empty() && params.planetFile == actor().planetFile);
    terrain::unreal::setDevFlags("volcanoes");
    params = actor();
    (void)terrain::unreal::applyDevFlags(params, kBase, kTable);
    TEST_CHECK(params.experimental == "volcanoes" && params.planetFile == actor().planetFile);
    TEST_CHECK((terrain::unreal::devFlagsLines(kTable) ==
                std::vector<FlagLine>{
                    {false, "Terrain.Flags: none for the next Plays of this editor, over the "
                            "setting of the actor"},
                    {true, "Terrain World: unknown experimental flag 'volcanoes' (known: alpha, "
                           "beta)"}}));
    terrain::unreal::clearDevFlags();
}

void testStartLine() {
    const terrain::AppliedFlags none = terrain::unreal::sessionFlags("").applied;
    TEST_CHECK((terrain::unreal::flagLines(none, terrain::experimentalFlags(), "Terrain.Flags") ==
                std::vector<FlagLine>{
                    {false, "Terrain World: experimental flags: none (from Terrain.Flags)"}}));
    const terrain::AppliedFlags both = terrain::unreal::sessionFlags("beta,alpha", kTable).applied;
    TEST_CHECK((terrain::unreal::flagLines(both, kTable, "Terrain.Flags") ==
                std::vector<FlagLine>{{false, "Terrain World: experimental flags: "
                                              "\"alpha,beta\" (from Terrain.Flags)"}}));
}

} // namespace

int main() {
    testParse();
    testNotSet();
    testSet();
    testNoneAndUnknown();
    testStartLine();
    return test::result();
}
