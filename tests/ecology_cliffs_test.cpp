// Ecology on the cliffs, overhangs and arches of the cliff site of the legacy relief (its place
// and plants, test_relief::legacyRelief, as the other tests of the cliff site): bare
// rock and no vegetation on the vertical walls, no land plant on rivers or under the sea,
// plants only on gentle ground of the exact surface, and the sculpted region unchanged.
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/sculpted_region.hpp"

#include "legacy_relief_support.hpp"
#include "test_assert.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using terrain::Ecology;
using terrain::EcologyMap;
using terrain::EcologySample;
using terrain::HeightGrid;
using terrain::kSeaLevel;
using terrain::Plant;
using terrain::PlantType;
using terrain::SculptedRegion;
using terrain::SurfaceMaterial;
using terrain::SurfaceSite;
using terrain::TerrainSurface;

// The cliff site of the default seed lies in an equatorial desert: no plant to place around
// its walls. This one has vegetation (most seeds do).
constexpr std::uint64_t kSeed = 1;

// Wall test of the cliff preview: a drop of at least half a stratum to a 4-neighbor sample,
// where the cliff mask of either sample is above 0.
bool isWall(const SculptedRegion& region, int column, int row) {
    const HeightGrid& surface = region.surface();
    const std::vector<float>& mask = region.cliffs().mask();
    const float drop = 0.5f * region.cliffs().field().params().stratumHeight;
    const std::size_t i = surface.index(column, row);
    for (const auto& [dc, dr] :
         std::array<std::array<int, 2>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
        const int c = column + dc;
        const int r = row + dr;
        if (c < 0 || r < 0 || c >= surface.size || r >= surface.size) {
            continue;
        }
        const std::size_t n = surface.index(c, r);
        if (std::abs(surface.heights[i] - surface.heights[n]) >= drop &&
            (mask[i] > 0.0f || mask[n] > 0.0f)) {
            return true;
        }
    }
    return false;
}

// The surface is the top of the rock of the sculpted region, bit for bit, with its rivers, its
// rock above air and its walls.
void testSurface(const SculptedRegion& region, const TerrainSurface& surface) {
    const auto& rivers = region.cliffs().eroded().rivers();
    bool same = true;
    int walls = 0;
    int air = 0;
    for (int row = 0; row < surface.size(); ++row) {
        for (int column = 0; column < surface.size(); ++column) {
            const std::size_t i = surface.index(column, row);
            const SurfaceSite& site = surface.sites[i];
            same = same && site.ground == region.surface().heights[i] &&
                   site.rockAboveAir == (region.airBelowRock()[i] != 0) &&
                   site.cliffWall == isWall(region, column, row) &&
                   (site.water == terrain::WaterKind::River) == rivers.isRiver(static_cast<int>(i));
            walls += site.cliffWall ? 1 : 0;
            air += site.rockAboveAir ? 1 : 0;
        }
    }
    TEST_CHECK(same);
    TEST_CHECK(walls > 20);
    TEST_CHECK(air > 0);
}

// Walls and rock above air are bare rock without vegetation; rivers and the ocean are water
// without land vegetation.
void testSamples(const TerrainSurface& surface, const EcologyMap& map) {
    bool walls = true;
    bool water = true;
    for (std::size_t i = 0; i < map.samples.size(); ++i) {
        const SurfaceSite& site = surface.sites[i];
        const EcologySample& sample = map.samples[i];
        if ((site.cliffWall || site.rockAboveAir) && site.dry()) {
            walls = walls && sample.material == SurfaceMaterial::Rock &&
                    terrain::totalDensity(sample.densities) == 0.0f;
        }
        if (!site.dry()) {
            const float seaweed = sample.densities[static_cast<std::size_t>(PlantType::Seaweed)];
            water = water && sample.material == SurfaceMaterial::Water &&
                    terrain::totalDensity(sample.densities) == seaweed;
        }
    }
    TEST_CHECK(walls);
    TEST_CHECK(water);
}

// Plants around the walls: never in a cell of the map touching a wall, a river or the ocean,
// always on the exact surface (SculptedRegion::surfaceAt), above the sea, on ground no steeper
// than their type allows.
void testPlantsAroundWalls(const SculptedRegion& region, const TerrainSurface& surface,
                           const Ecology& ecology, const EcologyMap& map) {
    const terrain::ReliefHeight height = [&region](double x, double z) {
        return region.surfaceAt(x, z);
    };
    const auto& params = ecology.params().vegetation;
    const double spacing = map.region.metersPerPixel;
    std::vector<Plant> plants;
    int areas = 0;
    std::size_t next = 0; // areas around walls at least 4 rows apart
    for (std::size_t i = 0; i < surface.sites.size() && areas < 6; ++i) {
        if (i < next || !surface.sites[i].cliffWall) {
            continue;
        }
        next = i + 4 * static_cast<std::size_t>(surface.size());
        const double x = surface.sampleX(static_cast<int>(i) % surface.size());
        const double z = surface.sampleZ(static_cast<int>(i) / surface.size());
        const std::vector<Plant> around =
            ecology.placePlants(map, height, {x - 120.0, z - 120.0, x + 120.0, z + 120.0});
        plants.insert(plants.end(), around.begin(), around.end());
        ++areas;
    }
    TEST_CHECK(areas >= 3);
    TEST_CHECK(plants.size() > 100);
    bool valid = true;
    for (const Plant& plant : plants) {
        const int column = static_cast<int>(std::floor((plant.x - map.sampleX(0)) / spacing));
        const int row = static_cast<int>(std::floor((plant.z - map.sampleZ(0)) / spacing));
        for (int r = row; r <= row + 1; ++r) {
            for (int c = column; c <= column + 1; ++c) {
                const SurfaceSite& site = surface.at(c, r);
                valid = valid && site.dry() && !site.cliffWall && !site.rockAboveAir;
            }
        }
        const double maxRise =
            params.rules[static_cast<std::size_t>(plant.type)].maxSlope * params.probeStep;
        for (const auto& [dx, dz] :
             std::array<std::array<double, 2>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
            const float probe =
                height(plant.x + dx * params.probeStep, plant.z + dz * params.probeStep);
            valid = valid && std::abs(static_cast<double>(probe) - plant.y) <= maxRise;
        }
        valid = valid && plant.type != PlantType::Seaweed && plant.y >= kSeaLevel &&
                plant.y == region.surfaceAt(plant.x, plant.z);
    }
    TEST_CHECK(valid);
}

} // namespace

int main() {
    const SculptedRegion region(
        kSeed, terrain::findCliffRegion(kSeed, 50.0, 129, {}, test_relief::legacyRelief()),
        test_relief::onLegacyRelief(terrain::SculptedRegionParams{}));
    const std::vector<float> surfaceBefore = region.surface().heights;
    const std::vector<float> cliffsBefore = region.cliffs().surface().heights;
    const std::vector<float> erodedBefore = region.cliffs().eroded().grid().heights;

    const TerrainSurface surface = terrain::surfaceFromSculpted(region);
    const Ecology ecology(kSeed);
    const EcologyMap map = ecology.map(surface);
    testSurface(region, surface);
    testSamples(surface, map);
    testPlantsAroundWalls(region, surface, ecology, map);

    // The ecology only reads the relief: every height of every layer is unchanged.
    TEST_CHECK(region.surface().heights == surfaceBefore);
    TEST_CHECK(region.cliffs().surface().heights == cliffsBefore);
    TEST_CHECK(region.cliffs().eroded().grid().heights == erodedBefore);
    return test::result();
}
