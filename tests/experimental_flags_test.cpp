// The experimental flags of the world (experimental_flags.hpp): an empty text leaves the default
// parameters (the world of main); "seas" switches the seas on and nothing else; names apart by
// commas or white space, their case ignored, each applied once; the canonical text sorted; an
// unknown name given back, never ignored in silence; the table sorted, its names well formed.

#include "terrain/generation/experimental_flags.hpp"

#include "test_assert.hpp"

#include <string>
#include <vector>

namespace {

using terrain::AppliedFlags;
using terrain::WorldParams;

void testEmpty() {
    for (const char* text : {"", " ", " , ;\t\n"}) {
        WorldParams p;
        const AppliedFlags a = terrain::applyExperimentalFlags(text, p);
        TEST_CHECK(a.text.empty() && a.unknown.empty());
        TEST_CHECK(!p.water.seas.enabled && p.water.ergs.enabled);
        TEST_CHECK(p.relief.geography.enabled);
    }
}

void testSeas() {
    for (const char* text : {"seas", " Seas ", "seas,seas", "SEAS;seas", "\tseas\n"}) {
        WorldParams p;
        const AppliedFlags a = terrain::applyExperimentalFlags(text, p);
        TEST_CHECK(a.text == "seas" && a.unknown.empty());
        TEST_CHECK(p.water.seas.enabled);
        TEST_CHECK(p.water.ergs.enabled && p.relief.geography.enabled);
    }
    TEST_CHECK(terrain::canonicalFlags("Seas") == "seas");
}

void testUnknown() {
    WorldParams p;
    const AppliedFlags a = terrain::applyExperimentalFlags("volcanoes, seas lows,volcanoes", p);
    TEST_CHECK(a.text == "seas");
    TEST_CHECK((a.unknown == std::vector<std::string>{"volcanoes", "lows"}));
    TEST_CHECK(p.water.seas.enabled);
    WorldParams q;
    const AppliedFlags b = terrain::applyExperimentalFlags("sea", q);
    TEST_CHECK(b.text.empty() && b.unknown.size() == 1 && !q.water.seas.enabled);
    TEST_CHECK(terrain::canonicalFlags("sea,seas") == "seas");
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
        // Each name alone is its own canonical text.
        TEST_CHECK(terrain::canonicalFlags(flags[i].name) == flags[i].name);
        names += (names.empty() ? "" : ", ") + std::string(flags[i].name);
    }
    TEST_CHECK(terrain::knownFlagNames() == names);
}

} // namespace

int main() {
    testEmpty();
    testSeas();
    testUnknown();
    testTable();
    return test::result();
}
