// Ecology on the default world (the planetary geography), at a coast, a mountain and a plain
// found by search: on maps of 262 km around them the map is the pointwise chain climate, biome,
// material, the ocean is exactly under the sea and water exactly on it, and together they show
// most biomes; around the coast and the mountain the plants are valid and stand exactly on
// Heightfield::heightAt; the heights are unchanged (the biomes never modify the height).
#include "terrain/generation/ecology.hpp"

#include "default_world_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>
#include <vector>

namespace {

using default_world::Place;
using terrain::Biome;
using terrain::Ecology;
using terrain::EcologyMap;
using terrain::EcologySample;
using terrain::HeightmapRegion;
using terrain::kSeaLevel;
using terrain::Plant;
using terrain::PlantType;
using terrain::SurfaceMaterial;
using terrain::SurfaceSite;
using terrain::TerrainSurface;
using terrain::WaterKind;

bool sameSample(const EcologySample& a, const EcologySample& b) {
    return a.climate.baseTemperature == b.climate.baseTemperature &&
           a.climate.humidity == b.climate.humidity &&
           a.climate.temperature == b.climate.temperature && a.climate.slope == b.climate.slope &&
           a.climate.exposure == b.climate.exposure && a.biome == b.biome &&
           a.material == b.material && a.densities == b.densities;
}

// The map of 128 x 128 samples of 2048 m around a place: the pointwise chain and the water
// rules hold; its biomes are counted into biomes.
void testCoherence(const Ecology& ecology, const Place& place,
                   std::array<int, terrain::kBiomeCount>& biomes) {
    const HeightmapRegion region{place.x, place.z, 2048.0, 128};
    const TerrainSurface surface = terrain::surfaceFromHeightfield(default_world::field(), region);
    const EcologyMap map = ecology.map(surface);
    const auto& params = ecology.params();
    bool chain = true;
    bool water = true;
    for (int row = 0; row < region.size; ++row) {
        for (int column = 0; column < region.size; ++column) {
            const SurfaceSite& site = surface.at(column, row);
            const EcologySample& s = map.at(column, row);
            chain =
                chain &&
                sameSample(s,
                           ecology.sampleAt(surface.sampleX(column), surface.sampleZ(row), site)) &&
                s.biome == terrain::classifyBiome(s.climate, site, params.biomes) &&
                s.material == terrain::surfaceMaterial(s.biome, s.climate, site, params.materials);
            water = water && (s.biome == Biome::Ocean) == (site.water == WaterKind::Ocean) &&
                    (s.material == SurfaceMaterial::Water) == !site.dry();
            ++biomes[static_cast<std::size_t>(s.biome)];
        }
    }
    TEST_CHECK(chain);
    TEST_CHECK(water);
}

// A plant is valid when the samples of the map around it allow its type and are dry for a land
// plant (in the sea for seaweed), and it stands exactly on the surface, above the sea for a land
// plant.
bool validPlant(const Ecology& ecology, const TerrainSurface& surface, const EcologyMap& map,
                const Plant& plant) {
    const double spacing = map.region.metersPerPixel;
    const int column = static_cast<int>(std::floor((plant.x - map.sampleX(0)) / spacing));
    const int row = static_cast<int>(std::floor((plant.z - map.sampleZ(0)) / spacing));
    const bool seaweed = plant.type == PlantType::Seaweed;
    for (const auto& [dc, dr] :
         std::array<std::array<int, 2>, 4>{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}}) {
        const SurfaceSite& site = surface.at(column + dc, row + dr);
        const EcologySample& sample = map.at(column + dc, row + dr);
        if (sample.densities[static_cast<std::size_t>(plant.type)] <= 0.0f ||
            !ecology.vegetation().canGrow(plant.type, sample.material, sample.climate, site) ||
            (seaweed ? site.water != WaterKind::Ocean : !site.dry())) {
            return false;
        }
    }
    return (seaweed || plant.y >= kSeaLevel) &&
           plant.y == default_world::field().heightAt(plant.x, plant.z);
}

// The plants of a square of 1 km at a place, on a map of 64 m, and the heights unchanged.
std::size_t testPlants(const Ecology& ecology, const Place& place) {
    const terrain::Heightfield& field = default_world::field();
    const HeightmapRegion region{place.x, place.z, 64.0, 64};
    const terrain::HeightGrid before = terrain::sampleHeightGrid(field, region);
    const TerrainSurface surface = terrain::surfaceFromHeightfield(field, region);
    const EcologyMap map = ecology.map(surface);
    const terrain::ReliefHeight height = [&field](double x, double z) {
        return field.heightAt(x, z);
    };
    const std::vector<Plant> plants = ecology.placePlants(
        map, height, {place.x - 500.0, place.z - 500.0, place.x + 500.0, place.z + 500.0});
    bool valid = true;
    for (const Plant& plant : plants) {
        valid = valid && validPlant(ecology, surface, map, plant);
    }
    TEST_CHECK(valid);
    bool sameGround = true;
    for (int row = 0; row < region.size; ++row) {
        for (int column = 0; column < region.size; ++column) {
            sameGround =
                sameGround && surface.at(column, row).ground ==
                                  field.heightAt(surface.sampleX(column), surface.sampleZ(row));
        }
    }
    TEST_CHECK(sameGround);
    TEST_CHECK(terrain::sampleHeightGrid(field, region).heights == before.heights);
    return plants.size();
}

} // namespace

int main() {
    const std::optional<Place> coast = default_world::coast();
    const std::optional<Place> mountain = default_world::mountain();
    const std::optional<Place> plain = default_world::plain();
    TEST_CHECK(coast && mountain && plain);
    if (!coast || !mountain || !plain) {
        return test::result();
    }
    const Ecology ecology(default_world::kSeed);
    std::array<int, terrain::kBiomeCount> biomes{};
    for (const Place& place : {*coast, *mountain, *plain}) {
        testCoherence(ecology, place, biomes);
    }
    const auto present = std::count_if(biomes.begin(), biomes.end(), [](int n) { return n > 0; });
    const std::size_t plants = testPlants(ecology, *coast) + testPlants(ecology, *mountain);
    std::printf("default world: coast (%.0f, %.0f), mountain (%.0f, %.0f), plain (%.0f, %.0f) km; "
                "%d biomes, %zu plants\n",
                coast->x / 1000.0, coast->z / 1000.0, mountain->x / 1000.0, mountain->z / 1000.0,
                plain->x / 1000.0, plain->z / 1000.0, static_cast<int>(present), plants);
    TEST_CHECK(present >= 8);
    TEST_CHECK(plants > 100);
    return test::result();
}
