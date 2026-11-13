// The flag "volcanoes" (agent 45) in the experimental flags of agent 51, as the provisional
// experimental_flags_support.hpp has them (the form of aeb5347): an empty text leaves the
// volcanoes off (the world of main); "volcanoes" switches them on and nothing else; names apart by
// commas or white space, their case ignored, each applied once; an unknown name given back; the
// table sorted, its names well formed. The same cases as experimental_flags_test of agent 51:
// they move there with the row, when its header reaches main, and this test goes.

#include "experimental_flags_support.hpp"

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
        TEST_CHECK(!p.relief.volcanoes.enabled && p.relief.geography.enabled);
    }
}

void testVolcanoes() {
    for (const char* text :
         {"volcanoes", " Volcanoes ", "volcanoes,volcanoes", "VOLCANOES;volcanoes", "\tvolcanoes\n"}) {
        WorldParams p;
        const AppliedFlags a = terrain::applyExperimentalFlags(text, p);
        TEST_CHECK(a.text == "volcanoes" && a.unknown.empty());
        TEST_CHECK(p.relief.volcanoes.enabled);
        // Nothing else: the parameters of main but the switch.
        WorldParams q;
        q.relief.volcanoes.enabled = true;
        TEST_CHECK(p.relief.volcanoes == q.relief.volcanoes && p.water.ergs.enabled &&
                   p.relief.geography.enabled);
    }
    TEST_CHECK(terrain::canonicalFlags("Volcanoes") == "volcanoes");
}

void testUnknown() {
    WorldParams p;
    const AppliedFlags a = terrain::applyExperimentalFlags("volcanos, volcanoes lava,volcanos", p);
    TEST_CHECK(a.text == "volcanoes");
    TEST_CHECK((a.unknown == std::vector<std::string>{"volcanos", "lava"}));
    TEST_CHECK(p.relief.volcanoes.enabled);
    WorldParams q;
    const AppliedFlags b = terrain::applyExperimentalFlags("volcano", q);
    TEST_CHECK(b.text.empty() && b.unknown.size() == 1 && !q.relief.volcanoes.enabled);
    TEST_CHECK(terrain::canonicalFlags("volcano,volcanoes") == "volcanoes");
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
    testVolcanoes();
    testUnknown();
    testTable();
    return test::result();
}
