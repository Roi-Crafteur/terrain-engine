// The rows of agent 46 in the table of the experimental flags of the world (agent 51,
// experimental_flags.hpp): "tablelands" and "karst" set their switch of the geography and nothing
// else, whatever the case, the separators and the repeats; their canonical text sorted; an unknown
// name given back (the table itself: experimental_flags_test). And the planet file of
// terrain_export_rock with flags: in its output folder, named by its flags (rock_planet.hpp).
#include "terrain/generation/experimental_flags.hpp"
#include "terrain/generation/heightfield.hpp"

#include "rock_planet.hpp"
#include "test_assert.hpp"

#include <filesystem>

#include <string>
#include <vector>

// The samples keep the size they have on main (the column cache and the batches hold them): the
// plateaus and the karst add no field to them while they are off.
static_assert(sizeof(terrain::HeightSample) == 72);

namespace {

using terrain::AppliedFlags;
using terrain::WorldParams;

// The parameters of the world are those of main but for the switches of agent 46.
bool otherwiseDefault(const WorldParams& p) {
    terrain::GeographyParams g = p.relief.geography;
    g.tablelands = false;
    g.karst = false;
    return g == terrain::GeographyParams{} && p.water.ergs.enabled;
}

void testEmpty() {
    for (const char* text : {"", " ", " , ;\t\n"}) {
        WorldParams p;
        const AppliedFlags a = terrain::applyExperimentalFlags(text, p);
        TEST_CHECK(a.text.empty() && a.unknown.empty());
        TEST_CHECK(!p.relief.geography.tablelands && !p.relief.geography.karst);
    }
}

void testRows() {
    for (const char* text : {"tablelands", " Tablelands ", "tablelands,tablelands", "TABLELANDS;"}) {
        WorldParams p;
        const AppliedFlags a = terrain::applyExperimentalFlags(text, p);
        TEST_CHECK(a.text == "tablelands" && a.unknown.empty());
        TEST_CHECK(p.relief.geography.tablelands && !p.relief.geography.karst);
        TEST_CHECK(otherwiseDefault(p));
    }
    for (const char* text : {"tablelands,karst", "karst tablelands", "Karst;TABLELANDS,karst"}) {
        WorldParams p;
        const AppliedFlags a = terrain::applyExperimentalFlags(text, p);
        TEST_CHECK(a.text == "karst,tablelands" && a.unknown.empty());
        TEST_CHECK(p.relief.geography.tablelands && p.relief.geography.karst);
        TEST_CHECK(otherwiseDefault(p));
    }
    TEST_CHECK(terrain::canonicalFlags("KARST") == "karst");
}

void testUnknown() {
    WorldParams p;
    const AppliedFlags a = terrain::applyExperimentalFlags("volcanoes, karst lows,volcanoes", p);
    TEST_CHECK(a.text == "karst");
    TEST_CHECK((a.unknown == std::vector<std::string>{"volcanoes", "lows"}));
    TEST_CHECK(p.relief.geography.karst && !p.relief.geography.tablelands);
    WorldParams q;
    const AppliedFlags b = terrain::applyExperimentalFlags("tableland", q);
    TEST_CHECK(b.text.empty() && b.unknown.size() == 1 && !q.relief.geography.tablelands);
}

// The planet file of terrain_export_rock: the one given, or one of the output folder, of its flags.
void testPlanetFile() {
    const std::filesystem::path out = "views";
    TEST_CHECK(rock_tool::planetFileOf(out, {}, "") == out / "planet.bin");
    TEST_CHECK(rock_tool::planetFileOf(out, {}, "tablelands") == out / "planet_tablelands.bin");
    TEST_CHECK(rock_tool::planetFileOf(out, {}, "karst,tablelands") ==
               out / "planet_karst+tablelands.bin");
    TEST_CHECK(rock_tool::planetFileOf(out, "worlds/a.bin", "tablelands") == "worlds/a.bin");
    TEST_CHECK(rock_tool::planetFileOf(out, {}, "tablelands").parent_path() == out);
}

} // namespace

int main() {
    testEmpty();
    testRows();
    testUnknown();
    testPlanetFile();
    return test::result();
}
