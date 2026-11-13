// The experimental flags of the world (experimental_flags.hpp), on a table of test (the table of
// the world is empty until an agent adds its row): an empty text leaves the default parameters
// (the world of main); a name sets what its row says and nothing else; names apart by commas,
// semicolons or white space, their case ignored, each applied once, the canonical text sorted
// whatever the order written; an unknown name given back, never ignored in silence; the table of
// the world sorted, its names well formed.

#include "terrain/generation/experimental_flags.hpp"

#include "test_assert.hpp"

#include <array>
#include <string>
#include <vector>

namespace {

using terrain::AppliedFlags;
using terrain::ExperimentalFlag;
using terrain::WorldParams;

// Three rows of test, sorted by name, each setting one parameter of its own.
constexpr std::array<ExperimentalFlag, 3> kTable = {{
    {"alpha", "the ergs off (test)", [](WorldParams& p) { p.water.ergs.enabled = false; }},
    {"beta", "a planet of 9 rivers (test)",
     [](WorldParams& p) { p.planet.rivers.minDischarge = 9.0; }},
    {"gamma", "snow from -2 degrees (test)",
     [](WorldParams& p) { p.ecology.biomes.snowTemperature = -2.0f; }},
}};

bool isDefault(const WorldParams& p) {
    const WorldParams d;
    return p.water.ergs.enabled == d.water.ergs.enabled &&
           p.planet.rivers.minDischarge == d.planet.rivers.minDischarge &&
           p.relief.geography.enabled == d.relief.geography.enabled &&
           p.ecology.biomes.snowTemperature == d.ecology.biomes.snowTemperature;
}

void testEmpty() {
    for (const char* text : {"", " ", " , ;\t\n"}) {
        WorldParams p;
        const AppliedFlags a = terrain::applyExperimentalFlags(text, p, kTable);
        TEST_CHECK(a.text.empty() && a.unknown.empty() && isDefault(p));
        WorldParams q;
        const AppliedFlags b = terrain::applyExperimentalFlags(text, q);
        TEST_CHECK(b.text.empty() && b.unknown.empty() && isDefault(q));
    }
}

void testKnown() {
    for (const char* text : {"alpha", " Alpha ", "alpha,alpha", "ALPHA;alpha", "\talpha\n"}) {
        WorldParams p;
        const AppliedFlags a = terrain::applyExperimentalFlags(text, p, kTable);
        TEST_CHECK(a.text == "alpha" && a.unknown.empty());
        TEST_CHECK(!p.water.ergs.enabled && p.planet.rivers.minDischarge != 9.0);
    }
    for (const char* text : {"beta,alpha", "alpha beta", "Beta;ALPHA,beta"}) {
        WorldParams p;
        const AppliedFlags a = terrain::applyExperimentalFlags(text, p, kTable);
        TEST_CHECK(a.text == "alpha,beta" && a.unknown.empty());
        TEST_CHECK(!p.water.ergs.enabled && p.planet.rivers.minDischarge == 9.0);
    }
    WorldParams g;
    const AppliedFlags c = terrain::applyExperimentalFlags("gamma", g, kTable);
    TEST_CHECK(c.text == "gamma" && g.ecology.biomes.snowTemperature == -2.0f);
    TEST_CHECK(g.water.ergs.enabled && g.planet.rivers.minDischarge != 9.0);
    TEST_CHECK(terrain::canonicalFlags("Beta gamma alpha", kTable) == "alpha,beta,gamma");
    TEST_CHECK(terrain::knownFlagNames(kTable) == "alpha, beta, gamma");
}

void testUnknown() {
    WorldParams p;
    const AppliedFlags a =
        terrain::applyExperimentalFlags("volcanoes, beta lows,volcanoes", p, kTable);
    TEST_CHECK(a.text == "beta");
    TEST_CHECK((a.unknown == std::vector<std::string>{"volcanoes", "lows"}));
    TEST_CHECK(p.planet.rivers.minDischarge == 9.0 && p.water.ergs.enabled);
    WorldParams q;
    const AppliedFlags b = terrain::applyExperimentalFlags("alph", q, kTable);
    TEST_CHECK(b.text.empty() && b.unknown.size() == 1 && isDefault(q));
    // The table of the world knows none of them yet, or only its own rows.
    WorldParams r;
    const AppliedFlags c = terrain::applyExperimentalFlags("alpha", r);
    TEST_CHECK(c.unknown.size() + (c.text.empty() ? 0 : 1) == 1);
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

// The table of the world: sorted, its names well formed, each its own canonical text.
void testTable() {
    const auto flags = terrain::experimentalFlags();
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
    testKnown();
    testUnknown();
    testTable();
    return test::result();
}
