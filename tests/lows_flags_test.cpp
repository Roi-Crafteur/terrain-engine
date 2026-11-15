// The experimental flag "lows" (agent 52, its row in the table of agent 51,
// experimental_flags.hpp; the case each agent keeps in its own tests): without it the lows are off,
// the world of main; "lows" switches every family on, the sunken basins of the geography and the
// breaches toward lower land of the planet level, and nothing else; names apart by commas or white
// space, their case ignored; an unknown name given back.
#include "terrain/generation/experimental_flags.hpp"

#include "test_assert.hpp"

#include <string>
#include <vector>

namespace {

using terrain::AppliedFlags;
using terrain::WorldParams;

void testOff() {
    WorldParams p;
    const AppliedFlags a = terrain::applyExperimentalFlags(" , ", p);
    TEST_CHECK(a.text.empty() && a.unknown.empty());
    TEST_CHECK(!p.relief.lows.enabled && !p.relief.geography.sunkenBasins &&
               !p.planet.breach.sealByOutlet);
}

void testLows() {
    for (const char* text : {"lows", " Lows ", "lows,lows", "LOWS;lows", "\tlows\n"}) {
        WorldParams p;
        const AppliedFlags a = terrain::applyExperimentalFlags(text, p);
        TEST_CHECK(a.text == "lows" && a.unknown.empty());
        TEST_CHECK(p.relief.lows.enabled && p.relief.lows.basins && p.relief.lows.grabens &&
                   p.relief.lows.craters && p.relief.lows.hollows);
        TEST_CHECK(p.relief.geography.sunkenBasins && p.planet.breach.sealByOutlet);
        TEST_CHECK(p.relief.geography.enabled && p.water.ergs.enabled);
    }
    TEST_CHECK(terrain::canonicalFlags("Lows") == "lows");
    WorldParams q;
    const AppliedFlags b = terrain::applyExperimentalFlags("low, lows craters", q);
    TEST_CHECK(b.text == "lows");
    TEST_CHECK((b.unknown == std::vector<std::string>{"low", "craters"}));
    TEST_CHECK(q.relief.lows.enabled);
    bool listed = false;
    for (const terrain::ExperimentalFlag& f : terrain::experimentalFlags()) {
        listed = listed || std::string(f.name) == "lows";
    }
    TEST_CHECK(listed && terrain::knownFlagNames().find("lows") != std::string::npos);
}

} // namespace

int main() {
    testOff();
    testLows();
    return test::result();
}
