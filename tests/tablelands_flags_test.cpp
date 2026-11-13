// The rows of agent 46 in the table of the experimental flags of the world
// (experimental_flags.hpp): "tablelands" and "karst" set their switch of the geography and
// nothing else; names apart by commas or white space, their case ignored, each applied once, the
// canonical text sorted; an unknown name given back; the table sorted, its names well formed.
// (Agent 46's copy of the mechanism of agent 51: its experimental_flags_test takes these cases when
// its pull request is on main.)
#include "terrain/generation/experimental_flags.hpp"
#include "terrain/generation/heightfield.hpp"

#include "test_assert.hpp"

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

bool wellFormed(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    for (const char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) {
            return false;
        }
    }
    return true;
}

void testTable() {
    const auto flags = terrain::experimentalFlags();
    TEST_CHECK(!flags.empty());
    std::string names;
    for (std::size_t i = 0; i < flags.size(); ++i) {
        TEST_CHECK(wellFormed(flags[i].name) && flags[i].what[0] != '\0');
        TEST_CHECK(flags[i].apply != nullptr);
        if (i > 0) {
            TEST_CHECK(std::string(flags[i - 1].name) < std::string(flags[i].name));
        }
        TEST_CHECK(terrain::canonicalFlags(flags[i].name) == flags[i].name);
        names += (names.empty() ? "" : ", ") + std::string(flags[i].name);
    }
    TEST_CHECK(terrain::knownFlagNames() == names);
}

} // namespace

int main() {
    testEmpty();
    testRows();
    testUnknown();
    testTable();
    return test::result();
}
