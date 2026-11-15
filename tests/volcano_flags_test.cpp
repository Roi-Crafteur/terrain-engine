// The flag "volcanoes" (agent 45) in the table of the experimental flags (experimental_flags.hpp,
// agent 51): no flag leaves the volcanoes off (the world of main); "volcanoes", whatever its case,
// switches them on and sets nothing else of the world; its name is that of the table, the one
// written in the planet files and Unreal levels. The reading of the text (separators, case,
// duplicates, unknown names) is experimental_flags_test's.

#include "terrain/generation/experimental_flags.hpp"

#include "test_assert.hpp"

#include <string>

namespace {

using terrain::AppliedFlags;
using terrain::WorldParams;

// Everything a flag may set but the switch of the volcanoes: as without any flag.
bool othersOff(const WorldParams& p) {
    const WorldParams none;
    terrain::VolcanoParams volcanoes = p.relief.volcanoes;
    volcanoes.enabled = none.relief.volcanoes.enabled;
    return volcanoes == none.relief.volcanoes &&
           p.relief.geography.enabled == none.relief.geography.enabled &&
           p.water.ergs.enabled == none.water.ergs.enabled;
}

void testOff() {
    WorldParams p;
    const AppliedFlags a = terrain::applyExperimentalFlags("", p);
    TEST_CHECK(a.text.empty() && a.unknown.empty());
    TEST_CHECK(!p.relief.volcanoes.enabled && othersOff(p));
}

void testOn() {
    for (const char* text : {"volcanoes", " Volcanoes ", "VOLCANOES"}) {
        WorldParams p;
        const AppliedFlags a = terrain::applyExperimentalFlags(text, p);
        TEST_CHECK(a.text == "volcanoes" && a.unknown.empty());
        TEST_CHECK(p.relief.volcanoes.enabled && othersOff(p));
    }
    TEST_CHECK(terrain::canonicalFlags("Volcanoes") == "volcanoes");
}

void testRow() {
    int rows = 0;
    for (const terrain::ExperimentalFlag& f : terrain::experimentalFlags()) {
        if (std::string(f.name) == "volcanoes") {
            ++rows;
            TEST_CHECK(f.what[0] != '\0' && f.apply != nullptr);
        }
    }
    TEST_CHECK(rows == 1);
    TEST_CHECK(terrain::knownFlagNames().find("volcanoes") != std::string::npos);
}

} // namespace

int main() {
    testOff();
    testOn();
    testRow();
    return test::result();
}
