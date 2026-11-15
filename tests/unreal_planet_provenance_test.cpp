// The provenance of the planet file in the Bridge (agent 55, task T4): the line of the log
// (describe) says "no provenance" for a file written before the fingerprint of its relief, which
// is read and never built again; a file with it is read without that mention. And the cost of the
// fingerprint, in milliseconds: alone with a geography built for it (a fresh Heightfield), then
// as a session reads its planet file (the default world of the fixture coast_planet), once with
// Geography::shared already built by the Heightfield of the session.
#include "planet_source.hpp"

#include "terrain/generation/relief_fingerprint.hpp"
#include "terrain/storage/planet_file.hpp"

#include "coast_planet_support.hpp"
#include "planet_golden_support.hpp"
#include "planet_sections.hpp"
#include "storage_test_support.hpp"

#include "test_assert.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>

namespace {

double millisecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

bool mentions(const std::string& line) {
    return line.find("no provenance") != std::string::npos;
}

void testLine(const std::filesystem::path& dir) {
    const terrain::PlanetLevel level(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                     golden::smallPlanetParams(2), golden::kSmallPlanet);
    const std::filesystem::path with = dir / "with.bin";
    const std::filesystem::path before = dir / "before.bin";
    terrain::savePlanetFile(with, level, terrain::kDefaultWorldSeed, golden::kSmallPlanet);
    terrain::storage::savePlanetFile(before, level, terrain::kDefaultWorldSeed,
                                     golden::kSmallPlanet, "",
                                     std::span<const terrain::storage::PlanetSection>{});
    terrain::WorldParams params;
    params.planet = golden::smallPlanetParams(2);
    const terrain::unreal::PlanetSource read = terrain::unreal::openPlanetSource(
        with, terrain::kDefaultWorldSeed, golden::kSmallPlanet, {}, params);
    const terrain::unreal::PlanetSource old = terrain::unreal::openPlanetSource(
        before, terrain::kDefaultWorldSeed, golden::kSmallPlanet, {}, params);
    const std::string readLine = terrain::unreal::describe(read, "with.bin");
    const std::string oldLine = terrain::unreal::describe(old, "before.bin");
    std::printf("%s\n%s\n", readLine.c_str(), oldLine.c_str());
    TEST_CHECK(!read.built && !mentions(readLine));
    TEST_CHECK(!old.built && mentions(oldLine) && !old.level->reliefFingerprint());
}

// The fingerprint of the default world: with a geography of its own, then with the one a session
// already holds; and the read of the planet file of the fixture as a session reads it.
void measureCost() {
    auto start = std::chrono::steady_clock::now();
    const std::uint64_t fresh =
        terrain::reliefFingerprint(terrain::Heightfield(terrain::kDefaultWorldSeed));
    const double alone = millisecondsSince(start);
    const terrain::Heightfield session(terrain::kDefaultWorldSeed); // the geography is shared now
    start = std::chrono::steady_clock::now();
    const std::uint64_t shared =
        terrain::reliefFingerprint(terrain::Heightfield(terrain::kDefaultWorldSeed));
    const double warm = millisecondsSince(start);
    TEST_CHECK(fresh == shared);
    start = std::chrono::steady_clock::now();
    const terrain::unreal::PlanetSource source = terrain::unreal::openPlanetSource(
        coast_planet::filePath(), terrain::kDefaultWorldSeed, terrain::WorldSize{});
    const double read = millisecondsSince(start);
    TEST_CHECK(!source.built);
    const char* provenance =
        source.level->reliefFingerprint() ? "with its fingerprint" : "no provenance";
    std::printf("the fingerprint of the default world: %.1f ms with a geography of its own, "
                "%.1f ms with Geography::shared already built; the planet file of the fixture "
                "read by the Bridge in %.0f ms (%s)\n",
                alone, warm, read, provenance);
}

} // namespace

int main() {
    const std::filesystem::path dir = storage_test::freshDirectory("terrain_unreal_provenance");
    testLine(dir);
    measureCost();
    std::filesystem::remove_all(dir);
    return test::result();
}
