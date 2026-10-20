// Ecology on the heightfield and on an eroded region: determinism, continuity with the
// heightfield (no seam between regions or areas, plants on the exact surface), coherent
// biomes and materials on real relief, plants only on valid surfaces, heights unchanged.
// Cliffs, overhangs and arches: ecology_cliffs_test.cpp.
#include "terrain/generation/ecology.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

namespace {

using terrain::Biome;
using terrain::Ecology;
using terrain::EcologyMap;
using terrain::EcologySample;
using terrain::HeightmapRegion;
using terrain::kSeaLevel;
using terrain::Plant;
using terrain::PlantArea;
using terrain::PlantType;
using terrain::SurfaceMaterial;
using terrain::SurfaceSite;
using terrain::TerrainSurface;
using terrain::WaterKind;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

// Coast closest to the origin on a lattice of 1024 m: ocean to the west, land to the east.
// Centers and spacings are powers of two, so sample coordinates are exact.
HeightmapRegion coastRegion(double metersPerPixel, int size) {
    const terrain::Heightfield field(kSeed);
    for (int radius = 0; radius <= 60; ++radius) {
        for (int j = -radius; j <= radius; ++j) {
            for (int i = -radius; i <= radius; ++i) {
                const double x = i * 1024.0;
                const double z = j * 1024.0;
                if (std::max(std::abs(i), std::abs(j)) == radius &&
                    field.heightAt(x, z) < kSeaLevel &&
                    field.heightAt(x + 1024.0, z) >= kSeaLevel) {
                    return {x, z, metersPerPixel, size};
                }
            }
        }
    }
    return {};
}

bool sameSample(const EcologySample& a, const EcologySample& b) {
    return a.climate.baseTemperature == b.climate.baseTemperature &&
           a.climate.humidity == b.climate.humidity &&
           a.climate.temperature == b.climate.temperature && a.climate.slope == b.climate.slope &&
           a.climate.exposure == b.climate.exposure && a.biome == b.biome &&
           a.material == b.material && a.densities == b.densities;
}

bool sameMap(const EcologyMap& a, const EcologyMap& b) {
    if (a.samples.size() != b.samples.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.samples.size(); ++i) {
        if (!sameSample(a.samples[i], b.samples[i])) {
            return false;
        }
    }
    return true;
}

// Square of side 2 * half around the center of the region of a map.
PlantArea centralArea(const EcologyMap& map, double half) {
    const terrain::HeightmapRegion& r = map.region;
    return {r.centerX - half, r.centerZ - half, r.centerX + half, r.centerZ + half};
}

// A plant is valid when every sample of the map around it allows its type (so none of them is
// a wall, a river, or water for a land plant), it stands on the exact surface, above the sea
// for a land plant and between the seaweed depths for seaweed, on ground no steeper than its
// type allows.
bool validPlant(const Ecology& ecology, const TerrainSurface& surface, const EcologyMap& map,
                const terrain::ReliefHeight& height, const Plant& plant) {
    const double spacing = map.region.metersPerPixel;
    const int column = static_cast<int>(std::floor((plant.x - map.sampleX(0)) / spacing));
    const int row = static_cast<int>(std::floor((plant.z - map.sampleZ(0)) / spacing));
    const auto t = static_cast<std::size_t>(plant.type);
    const bool seaweed = plant.type == PlantType::Seaweed;
    for (const auto& [dc, dr] :
         std::array<std::array<int, 2>, 4>{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}}) {
        const SurfaceSite& site = surface.at(column + dc, row + dr);
        const EcologySample& sample = map.at(column + dc, row + dr);
        if (sample.densities[t] <= 0.0f ||
            !ecology.vegetation().canGrow(plant.type, sample.material, sample.climate, site) ||
            (seaweed ? site.water != WaterKind::Ocean : !site.dry())) {
            return false;
        }
    }
    const auto& params = ecology.params().vegetation;
    const float depth = kSeaLevel - plant.y;
    const bool level = seaweed ? depth >= params.minSeaweedDepth && depth <= params.maxSeaweedDepth
                               : plant.y >= kSeaLevel;
    const double maxRise = params.rules[t].maxSlope * params.probeStep;
    bool gentle = true;
    for (const auto& [dx, dz] :
         std::array<std::array<double, 2>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
        const float probe =
            height(plant.x + dx * params.probeStep, plant.z + dz * params.probeStep);
        gentle = gentle && std::abs(static_cast<double>(probe) - plant.y) <= maxRise;
    }
    return level && gentle && plant.y == height(plant.x, plant.z);
}

void testDeterminism() {
    const terrain::Heightfield field(kSeed);
    const TerrainSurface surface = terrain::surfaceFromHeightfield(field, coastRegion(64.0, 48));
    const Ecology a(kSeed);
    const Ecology b(kSeed);
    const Ecology other(kSeed + 1);
    const EcologyMap mapA = a.map(surface);
    const EcologyMap mapB = b.map(surface);
    TEST_CHECK(sameMap(mapA, mapB));
    TEST_CHECK(!sameMap(mapA, other.map(surface)));
    const terrain::ReliefHeight height = [&field](double x, double z) {
        return field.heightAt(x, z);
    };
    const PlantArea area = centralArea(mapA, 400.0);
    const std::vector<Plant> plants = a.placePlants(mapA, height, area);
    TEST_CHECK(!plants.empty());
    TEST_CHECK(plants == b.placePlants(mapB, height, area));
    TEST_CHECK(plants != other.placePlants(other.map(surface), height, area));
}

// Two overlapping regions of the heightfield give the same surface, ecology and plants where
// they overlap; placing two halves of an area gives the plants of the whole area; plants stand
// exactly on Heightfield::heightAt; the heights are unchanged.
void testContinuityWithHeightfield() {
    const terrain::Heightfield field(kSeed);
    const HeightmapRegion a = coastRegion(64.0, 64);
    HeightmapRegion b = a;
    b.centerX += 20 * 64.0;
    b.centerZ += 12 * 64.0;
    const terrain::HeightGrid before = terrain::sampleHeightGrid(field, a);
    const Ecology ecology(kSeed);
    const TerrainSurface surfaceA = terrain::surfaceFromHeightfield(field, a);
    const TerrainSurface surfaceB = terrain::surfaceFromHeightfield(field, b);
    const EcologyMap mapA = ecology.map(surfaceA);
    const EcologyMap mapB = ecology.map(surfaceB);
    bool seamless = true;
    for (int row = 12; row < a.size; ++row) {
        for (int column = 20; column < a.size; ++column) {
            const SurfaceSite& sa = surfaceA.at(column, row);
            const SurfaceSite& sb = surfaceB.at(column - 20, row - 12);
            seamless = seamless && sa.ground == sb.ground && sa.gradX == sb.gradX &&
                       sa.gradZ == sb.gradZ && sa.steepness == sb.steepness &&
                       sameSample(mapA.at(column, row), mapB.at(column - 20, row - 12));
        }
    }
    TEST_CHECK(seamless);

    const terrain::ReliefHeight height = [&field](double x, double z) {
        return field.heightAt(x, z);
    };
    // A square of the overlap along the first samples of B.
    const PlantArea overlap{mapB.sampleX(0), mapB.sampleZ(0), mapB.sampleX(0) + 1000.0,
                            mapB.sampleZ(0) + 1000.0};
    const std::vector<Plant> plants = ecology.placePlants(mapA, height, overlap);
    TEST_CHECK(plants.size() > 100);
    TEST_CHECK(plants == ecology.placePlants(mapB, height, overlap));

    const double middle = std::floor((overlap.minZ + overlap.maxZ) / 8.0) * 4.0;
    std::vector<Plant> halves =
        ecology.placePlants(mapA, height, {overlap.minX, overlap.minZ, overlap.maxX, middle});
    const std::vector<Plant> south =
        ecology.placePlants(mapA, height, {overlap.minX, middle, overlap.maxX, overlap.maxZ});
    halves.insert(halves.end(), south.begin(), south.end());
    TEST_CHECK(halves == plants);

    bool valid = true;
    std::set<PlantType> types;
    for (const Plant& plant : plants) {
        valid = valid && validPlant(ecology, surfaceA, mapA, height, plant);
        types.insert(plant.type);
    }
    TEST_CHECK(valid);
    TEST_CHECK(types.size() >= 3);
    TEST_CHECK(terrain::sampleHeightGrid(field, a).heights == before.heights);
}

// On a 262 km map of the heightfield: the map is exactly the pointwise chain climate, biome,
// material and densities; the ocean is exactly under the sea, water exactly on it, and most
// biomes show up.
void testCoherenceOnHeightfield() {
    const terrain::Heightfield field(kSeed);
    const HeightmapRegion region{.metersPerPixel = 2048.0, .size = 128};
    const TerrainSurface surface = terrain::surfaceFromHeightfield(field, region);
    const Ecology ecology(kSeed);
    const EcologyMap map = ecology.map(surface);
    const auto& params = ecology.params();
    bool chain = true;
    bool water = true;
    std::array<int, terrain::kBiomeCount> biomes{};
    for (int row = 0; row < region.size; ++row) {
        for (int column = 0; column < region.size; ++column) {
            const SurfaceSite& site = surface.at(column, row);
            const EcologySample& s = map.at(column, row);
            const double x = surface.sampleX(column);
            const double z = surface.sampleZ(row);
            chain = chain && sameSample(s, ecology.sampleAt(x, z, site)) &&
                    s.biome == terrain::classifyBiome(s.climate, site, params.biomes) &&
                    s.material ==
                        terrain::surfaceMaterial(s.biome, s.climate, site, params.materials) &&
                    s.climate.temperature ==
                        ecology.climate().temperatureAt(s.climate.baseTemperature, site.waterLevel,
                                                        site.dry() ? s.climate.exposure : 0.0f);
            water = water && (s.biome == Biome::Ocean) == (site.water == WaterKind::Ocean) &&
                    (s.material == SurfaceMaterial::Water) == !site.dry();
            ++biomes[static_cast<std::size_t>(s.biome)];
        }
    }
    TEST_CHECK(chain);
    TEST_CHECK(water);
    const auto present = std::count_if(biomes.begin(), biomes.end(), [](int n) { return n > 0; });
    TEST_CHECK(present >= 10);
}

// An eroded region: rivers carry water and no land plant, plants stand on ErodedRegion::heightAt
// only on valid surfaces, and the region is unchanged.
void testErodedRegion() {
    const terrain::ErodedRegion eroded(kSeed, coastRegion(100.0, 96));
    const std::vector<float> grid = eroded.grid().heights;
    const std::vector<std::uint8_t> rivers = eroded.rivers().river;
    const TerrainSurface surface = terrain::surfaceFromEroded(eroded);
    const Ecology ecology(kSeed);
    const EcologyMap map = ecology.map(surface);
    bool water = true;
    int riverSample = -1; // the river sample closest to the center
    double closest = 1e30;
    for (std::size_t i = 0; i < map.samples.size(); ++i) {
        const SurfaceSite& site = surface.sites[i];
        const EcologySample& s = map.samples[i];
        const float landDensity = terrain::totalDensity(s.densities) -
                                  s.densities[static_cast<std::size_t>(PlantType::Seaweed)];
        water = water && (s.material == SurfaceMaterial::Water) == !site.dry() &&
                (s.biome == Biome::Ocean) == (site.water == WaterKind::Ocean) &&
                (site.dry() || landDensity == 0.0f);
        const int column = static_cast<int>(i) % surface.size();
        const int row = static_cast<int>(i) / surface.size();
        const double distance = std::hypot(surface.sampleX(column) - surface.region.centerX,
                                           surface.sampleZ(row) - surface.region.centerZ);
        if (site.water == WaterKind::River && distance < closest) {
            riverSample = static_cast<int>(i);
            closest = distance;
        }
    }
    TEST_CHECK(water);
    TEST_CHECK(riverSample >= 0);

    // Plants within 600 m of the river sample.
    const double x = surface.sampleX(riverSample % surface.size());
    const double z = surface.sampleZ(riverSample / surface.size());
    const terrain::ReliefHeight height = [&eroded](double px, double pz) {
        return eroded.heightAt(px, pz);
    };
    const std::vector<Plant> plants =
        ecology.placePlants(map, height, {x - 600.0, z - 600.0, x + 600.0, z + 600.0});
    TEST_CHECK(plants.size() > 1000);
    bool valid = true;
    for (const Plant& plant : plants) {
        valid = valid && validPlant(ecology, surface, map, height, plant);
    }
    TEST_CHECK(valid);
    TEST_CHECK(eroded.grid().heights == grid);
    TEST_CHECK(eroded.rivers().river == rivers);
}

} // namespace

int main() {
    testDeterminism();
    testContinuityWithHeightfield();
    testCoherenceOnHeightfield();
    testErodedRegion();
    return test::result();
}
