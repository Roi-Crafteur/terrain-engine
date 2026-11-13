// The experimental flags of task 11C (agent 31; experimental_flags.hpp): each off by default (the
// world of main), each switching on what its row says and nothing else; the planet file without a
// flag the one of main (version 1), with planet_network of version 2, its discharges in their
// section, read back bit for bit; a file of other flags refused.
#include "terrain/generation/experimental_flags.hpp"
#include "terrain/storage/planet_file.hpp"

#include "network_flags_support.hpp"
#include "planet_golden_support.hpp"
#include "storage_test_support.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using terrain::PlanetLevel;
using terrain::PlanetParams;
using terrain::WorldParams;

bool same(const PlanetParams& a, const PlanetParams& b) {
    return a.climate.smoothBands == b.climate.smoothBands && a.links.enabled == b.links.enabled &&
           a.landDischarge == b.landDischarge;
}

void testTable() {
    const WorldParams none;
    TEST_CHECK(!none.planet.climate.smoothBands && !none.planet.links.enabled &&
               !none.planet.landDischarge);
    const auto flags = terrain::experimentalFlags();
    for (std::size_t k = 1; k < flags.size(); ++k) {
        TEST_CHECK(std::strcmp(flags[k - 1].name, flags[k].name) < 0); // sorted by name
    }
    struct Case {
        const char* name;
        bool bands;
        bool links;
        bool discharge;
    };
    for (const Case c : {Case{"smooth_bands", true, false, false},
                         Case{"pass_routing", false, true, false},
                         Case{"planet_network", false, false, true}}) {
        WorldParams p;
        const terrain::AppliedFlags applied = terrain::applyExperimentalFlags(c.name, p);
        TEST_CHECK(applied.text == c.name && applied.unknown.empty());
        TEST_CHECK(p.planet.climate.smoothBands == c.bands && p.planet.links.enabled == c.links &&
                   p.planet.landDischarge == c.discharge);
    }
    WorldParams all;
    const terrain::AppliedFlags applied =
        terrain::applyExperimentalFlags(" Smooth_Bands;planet_network pass_routing,lows ", all);
    TEST_CHECK(applied.text == network_flags::kStepOne);
    TEST_CHECK(applied.unknown == std::vector<std::string>{"lows"});
    TEST_CHECK(same(all.planet, network_flags::flagged(PlanetParams{}, network_flags::kStepOne)));
}

std::uint32_t versionOf(const std::filesystem::path& file) {
    const std::vector<std::uint8_t> bytes = storage_test::fileBytes(file);
    return static_cast<std::uint32_t>(bytes[4]) | static_cast<std::uint32_t>(bytes[5]) << 8 |
           static_cast<std::uint32_t>(bytes[6]) << 16 | static_cast<std::uint32_t>(bytes[7]) << 24;
}

void testFile(const std::filesystem::path& dir) {
    const PlanetParams params = network_flags::flagged(golden::smallPlanetParams(2, 2),
                                                       "planet_network");
    const std::string flags = terrain::canonicalFlags("planet_network");
    const std::filesystem::path file = dir / "network.bin";
    bool built = false;
    const PlanetLevel level = terrain::openPlanetLevel(
        file, terrain::kDefaultWorldSeed, terrain::HeightfieldParams{}, params,
        golden::kSmallPlanet, &built, {}, flags);
    TEST_CHECK(built && versionOf(file) == 2);
    TEST_CHECK(level.cells().landDischarge.size() > 1000);
    const PlanetLevel back =
        terrain::loadPlanetFile(file, terrain::kDefaultWorldSeed, params, golden::kSmallPlanet,
                                flags);
    TEST_CHECK(back.checksum() == level.checksum());
    TEST_CHECK(back.cells().landDischarge == level.cells().landDischarge);
    // Asked without the flag: refused, never built again over it.
    bool refused = false;
    try {
        (void)terrain::loadPlanetFile(file, terrain::kDefaultWorldSeed,
                                      golden::smallPlanetParams(2, 2), golden::kSmallPlanet);
    } catch (const terrain::PlanetFlagsError& e) {
        refused = e.stored() == "planet_network" && e.wanted().empty();
    }
    TEST_CHECK(refused);
    // Without a flag: the level, its checksum and its file (version 1) of main.
    const PlanetLevel plain(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                            golden::smallPlanetParams(2, 2), golden::kSmallPlanet);
    TEST_CHECK(plain.cells().landDischarge.empty());
    TEST_CHECK(plain.checksum() == 0xCDBFA06D845540C8ULL);
    terrain::savePlanetFile(dir / "plain.bin", plain, terrain::kDefaultWorldSeed,
                            golden::kSmallPlanet);
    TEST_CHECK(versionOf(dir / "plain.bin") == 1);
}

} // namespace

int main() {
    testTable();
    const std::filesystem::path dir = storage_test::freshDirectory("terrain_network_flags_test");
    testFile(dir);
    std::filesystem::remove_all(dir);
    return test::result();
}
