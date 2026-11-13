// The experimental flags of an Unreal level (agent 51, Bridge/session_flags.hpp): the lines of the
// log, word for word (none, the canonical text, a warning per unknown name); a session without
// flags or with an unknown one is the world of main (an unknown name changes nothing); a flag of
// a table of test reaches the relief and the ecology of the tiles of the session; a planet file
// built with other flags is refused by the session and by the planet job of Terrain.Seed, its
// bytes kept, nothing built over it nor beside it, and read by the session of its flags.

#include "planet_job.hpp"
#include "session_flags.hpp"
#include "terrain_session.hpp"

#include "storage_test_support.hpp"
#include "test_assert.hpp"

#include "terrain/storage/planet_file.hpp"

#include <array>
#include <bit>
#include <span>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using terrain::ExperimentalFlag;
using terrain::WorldParams;
using terrain::unreal::FlagLine;
using terrain::unreal::SessionParams;
using terrain::unreal::TerrainSession;

constexpr std::array<ExperimentalFlag, 2> kTable = {{
    {"alpha", "the mask of the mountains from -0.2 (test)",
     [](WorldParams& p) { p.relief.maskLow = -0.2f; }},
    {"beta", "snow from -2 degrees (test)",
     [](WorldParams& p) { p.ecology.biomes.snowTemperature = -2.0f; }},
}};

const terrain::WorldSize kSmall{32.0 * 65536.0, 16.0 * 65536.0};

SessionParams smallSession(const std::string& experimental) {
    SessionParams params;
    params.world = kSmall;
    params.stream.lod = {{2, 4, 8}, 6};
    params.stream.workerThreads = 2;
    params.stream.maxLoadsPerUpdate = 16;
    params.experimental = experimental;
    return params;
}

// The lines on an empty table (the table of the world before its rows) and on a table of test:
// they do not depend on the rows of the world.
void testLines() {
    using terrain::unreal::flagLines;
    using terrain::unreal::sessionFlags;
    constexpr std::span<const ExperimentalFlag> kNone{};
    TEST_CHECK((flagLines(sessionFlags("", kNone).applied, kNone) ==
                std::vector<FlagLine>{{false, "Terrain World: experimental flags: none"}}));
    TEST_CHECK((flagLines(sessionFlags("volcanoes", kNone).applied, kNone) ==
                std::vector<FlagLine>{
                    {false, "Terrain World: experimental flags: none"},
                    {true, "Terrain World: unknown experimental flag 'volcanoes' (no flag known "
                           "yet)"}}));
    TEST_CHECK((flagLines(sessionFlags("Beta alpha nope", kTable).applied, kTable) ==
                std::vector<FlagLine>{
                    {false, "Terrain World: experimental flags: \"alpha,beta\""},
                    {true, "Terrain World: unknown experimental flag 'nope' (known: alpha, "
                           "beta)"}}));
}

bool sameHeights(const TerrainSession& a, const TerrainSession& b) {
    bool same = true;
    for (int k = 0; k < 16; ++k) {
        const double x = -1'000'000.0 + 131'071.0 * k;
        const double z = -480'000.0 + 61'333.0 * ((k * 7) % 16);
        same = same && std::bit_cast<std::uint32_t>(a.tiles().heightfield().heightAt(x, z)) ==
                           std::bit_cast<std::uint32_t>(b.tiles().heightfield().heightAt(x, z));
    }
    return same;
}

// Without flags, with an unknown one: the world of main. A flag of the table of test: its relief
// and its ecology reach the tiles.
void testSessions() {
    const TerrainSession none(smallSession(""));
    TEST_CHECK(none.flags().applied.text.empty() && none.flags().applied.unknown.empty());
    const TerrainSession unknown(smallSession("no-such-flag"));
    TEST_CHECK((unknown.flags().applied.unknown == std::vector<std::string>{"no-such-flag"}));
    TEST_CHECK(sameHeights(none, unknown));
    SessionParams params = smallSession("alpha,beta");
    params.flagTable = kTable;
    const TerrainSession flagged(params);
    TEST_CHECK(flagged.flags().applied.text == "alpha,beta");
    TEST_CHECK(flagged.tiles().heightfield().params().maskLow == -0.2f);
    TEST_CHECK(flagged.tiles().ecology().params().biomes.snowTemperature == -2.0f);
    TEST_CHECK(none.tiles().ecology().params().biomes.snowTemperature != -2.0f);
    TEST_CHECK(none.tiles().heightfield().params().maskLow != -0.2f);
}

// A planet file of the flags "alpha": refused without them, its bytes kept, nothing built beside
// it by the planet job; read by a session of these flags.
void testPlanetRefused() {
    const std::filesystem::path dir = storage_test::freshDirectory("terrain_experimental_flags");
    const std::filesystem::path file = dir / "planet_alpha.bin";
    WorldParams alpha;
    (void)terrain::applyExperimentalFlags("alpha", alpha, kTable);
    const terrain::unreal::PlanetSource made = terrain::unreal::openPlanetSource(
        file, terrain::kDefaultWorldSeed, kSmall, {}, alpha, "alpha");
    TEST_CHECK(made.built && made.written);
    const std::vector<std::uint8_t> bytes = storage_test::fileBytes(file);
    SessionParams params = smallSession("");
    params.planetFile = file.string();
    bool refused = false;
    try {
        const TerrainSession session(params);
    } catch (const terrain::PlanetFlagsError& e) {
        refused = e.stored() == "alpha" && e.wanted().empty();
        std::printf("refused: %s\n", e.what());
    }
    TEST_CHECK(refused && storage_test::fileBytes(file) == bytes);
    const terrain::unreal::PlanetChoice choice =
        terrain::unreal::choosePlanetFile(file.string(), terrain::kDefaultWorldSeed, dir.string());
    terrain::unreal::PlanetJob job(choice, terrain::kDefaultWorldSeed, kSmall, "");
    const terrain::unreal::PlanetJobResult result = job.take();
    TEST_CHECK(result.error.find("experimental flags") != std::string::npos);
    TEST_CHECK(!std::filesystem::exists(choice.fallback) && storage_test::fileBytes(file) == bytes);
    params.experimental = "alpha";
    params.flagTable = kTable;
    const TerrainSession read(params);
    TEST_CHECK(!read.planet().built && read.planet().level != nullptr);
    TEST_CHECK(read.planet().level->checksum() == made.level->checksum());
    std::filesystem::remove_all(dir);
}

} // namespace

int main() {
    testLines();
    testSessions();
    testPlanetRefused();
    return test::result();
}
