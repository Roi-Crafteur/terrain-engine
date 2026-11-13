// The latitude of a sphere for the climate (climate_latitude.hpp; flag sphere_latitude, task 11C
// after the audit): off, the formulas of main bit for bit; on, sin(lat) = z / zmax, the tropics
// wide and the caps small, the climate of a cell at the mean altitude of its points, one
// threshold of the frost, the rivers going on under the ice.
#include "terrain/generation/climate.hpp"
#include "terrain/generation/climate_latitude.hpp"
#include "terrain/generation/experimental_flags.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/storage/planet_file.hpp"

#include "planet_golden_support.hpp"
#include "storage_test_support.hpp"

#include "test_assert.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>

namespace {

using terrain::climateLatitude;
using terrain::climateSine;

// Off, the expressions of main; on, those of a sphere.
void testLatitude() {
    const double period = 36'000'000.0;
    bool same = true;
    for (int k = -1000; k < 1000; ++k) {
        const double z = (k + 0.37) * period / 2000.0;
        same = same && std::bit_cast<std::uint64_t>(climateSine(z, period, false)) ==
                           std::bit_cast<std::uint64_t>(
                               terrain::dmath::sinTurns(z / (2.0 * period)));
        same = same && std::bit_cast<std::uint64_t>(std::abs(climateLatitude(z, period, false))) ==
                           std::bit_cast<std::uint64_t>(180.0 * std::abs(z) / period);
    }
    TEST_CHECK(same);
    // Half of the way from the equator to a pole: 30 degrees on a sphere (45 off).
    TEST_CHECK(std::abs(climateLatitude(period / 4.0, period, true) - 30.0) < 1e-9);
    TEST_CHECK(std::abs(climateLatitude(-period / 4.0, period, true) + 30.0) < 1e-9);
    TEST_CHECK(std::abs(climateLatitude(period / 4.0, period, false) - 45.0) < 1e-9);
    TEST_CHECK(std::abs(climateLatitude(period / 2.0, period, true) - 90.0) < 1e-9);
    TEST_CHECK(std::abs(climateSine(period / 4.0, period, true) - 0.5) < 1e-12);
}

// The temperature of the tiles: warmer at mid z with a sphere, the same at the equator and the
// poles.
void testTiles() {
    terrain::ClimateParams off;
    terrain::ClimateParams on;
    on.sphereLatitude = true;
    const terrain::WorldSize world;
    const terrain::ClimateField a(terrain::kDefaultWorldSeed, off, world);
    const terrain::ClimateField b(terrain::kDefaultWorldSeed, on, world);
    const double mid = 0.25 * world.z;
    TEST_CHECK(b.latitudeTemperatureAt(mid) > a.latitudeTemperatureAt(mid) + 5.0f);
    TEST_CHECK(std::abs(b.latitudeTemperatureAt(0.0) - a.latitudeTemperatureAt(0.0)) < 1e-4f);
    TEST_CHECK(std::abs(b.latitudeTemperatureAt(0.4999 * world.z) -
                        a.latitudeTemperatureAt(0.4999 * world.z)) < 0.2f);
}

// The small world with the flag: the parameters it sets, a climate at the mean altitude (never
// below the floor), fewer frozen cells, the level read back with its rise.
void testSmallWorld() {
    terrain::WorldParams world;
    world.planet = golden::smallPlanetParams(2, 2);
    const terrain::AppliedFlags applied = terrain::applyExperimentalFlags("sphere_latitude", world);
    TEST_CHECK(applied.text == "sphere_latitude");
    TEST_CHECK(world.relief.sphereLatitude && world.ecology.climate.sphereLatitude);
    TEST_CHECK(world.planet.climate.sphereLatitude && world.planet.climate.meanAltitude);
    TEST_CHECK(world.planet.rivers.underIce);
    TEST_CHECK(world.planet.climate.iceTemperature == world.ecology.biomes.snowTemperature);
    const terrain::PlanetLevel on(terrain::kDefaultWorldSeed, world.relief, world.planet,
                                  golden::kSmallPlanet);
    const terrain::PlanetLevel off(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                   golden::smallPlanetParams(2, 2), golden::kSmallPlanet);
    TEST_CHECK(off.cells().climateRise.empty());
    TEST_CHECK(!on.cells().climateRise.empty());
    std::size_t land = 0;
    std::size_t risen = 0;
    std::size_t frozenOn = 0;
    std::size_t frozenOff = 0;
    bool above = true;
    for (std::size_t c = 0; c < on.grid().cells(); ++c) {
        if (on.cells().ocean(c)) {
            continue;
        }
        ++land;
        above = above && on.climateElevation(c) >= on.elevation(c);
        risen += on.climateElevation(c) > on.elevation(c) ? 1 : 0;
        frozenOn += on.water(c).frozen ? 1 : 0;
        frozenOff += !off.cells().ocean(c) && off.water(c).frozen ? 1 : 0;
    }
    std::printf("small world with sphere_latitude: %zu land cells, %zu with a climate above their "
                "floor; frozen %zu (off: %zu)\n",
                land, risen, frozenOn, frozenOff);
    TEST_CHECK(above && risen > land / 10);
    TEST_CHECK(frozenOn < frozenOff);
    // Written and read back with its rise (a file of version 2, its flags first).
    const std::filesystem::path dir = storage_test::freshDirectory("terrain_sphere_latitude_test");
    const std::filesystem::path file = dir / "sphere.bin";
    terrain::savePlanetFile(file, on, terrain::kDefaultWorldSeed, golden::kSmallPlanet,
                            applied.text);
    const terrain::PlanetLevel back = terrain::loadPlanetFile(
        file, terrain::kDefaultWorldSeed, world.planet, golden::kSmallPlanet, applied.text);
    TEST_CHECK(back.checksum() == on.checksum());
    TEST_CHECK(back.cells().climateRise == on.cells().climateRise);
    std::filesystem::remove_all(dir);
}

} // namespace

int main() {
    testLatitude();
    testTiles();
    testSmallWorld();
    return test::result();
}
