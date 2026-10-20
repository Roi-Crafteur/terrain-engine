// Vegetation: where each type may grow, densities, clumps and the draws of the placement
// lattice. Placement on real relief: ecology_test.cpp and ecology_cliffs_test.cpp.
#include "terrain/generation/vegetation.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <vector>

namespace {

using terrain::Biome;
using terrain::ClimateSample;
using terrain::LatticeDraw;
using terrain::PlantDensities;
using terrain::PlantType;
using terrain::SurfaceMaterial;
using terrain::SurfaceSite;
using terrain::VegetationField;
using terrain::WaterKind;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

ClimateSample climateOf(float slope) {
    ClimateSample climate;
    climate.temperature = 15.0f;
    climate.humidity = 0.5f;
    climate.slope = slope;
    return climate;
}

SurfaceSite landSite(float steepness = 0.0f) {
    SurfaceSite site;
    site.ground = 200.0f;
    site.waterLevel = 200.0f;
    site.steepness = steepness;
    return site;
}

SurfaceSite oceanSite(float depth) {
    SurfaceSite site;
    site.ground = terrain::kSeaLevel - depth;
    site.waterLevel = terrain::kSeaLevel;
    site.water = WaterKind::Ocean;
    return site;
}

SurfaceSite riverSite() {
    SurfaceSite site = landSite();
    site.water = WaterKind::River;
    site.riverDistance = 0.0f;
    return site;
}

PlantType typeAt(int index) {
    return static_cast<PlantType>(index);
}

std::size_t slot(PlantType type) {
    return static_cast<std::size_t>(type);
}

void testNamesAndTables() {
    for (int a = 0; a < terrain::kPlantTypeCount; ++a) {
        const char* name = terrain::plantName(typeAt(a));
        TEST_CHECK(name != nullptr && std::strlen(name) > 0);
        for (int b = 0; b < a; ++b) {
            TEST_CHECK(std::strcmp(name, terrain::plantName(typeAt(b))) != 0);
        }
    }
    // No type grows on ground as steep as bare rock: never on a cliff face.
    const terrain::MaterialParams materials;
    for (const terrain::PlantRule& rule : terrain::kDefaultPlantRules) {
        TEST_CHECK(rule.maxSlope > 0.0f && rule.maxSlope < materials.rockSlope);
    }
    // Only seaweed grows in water, and only on water.
    for (int t = 0; t < terrain::kPlantTypeCount; ++t) {
        const bool water = (terrain::kDefaultPlantRules[static_cast<std::size_t>(t)].materials &
                            terrain::materialBit(SurfaceMaterial::Water)) != 0;
        TEST_CHECK(water == (typeAt(t) == PlantType::Seaweed));
    }
    for (const PlantDensities& row : terrain::kDefaultBiomeDensity) {
        TEST_CHECK(terrain::totalDensity(row) <= 1.0f);
    }
}

// Land types: only on dry, solid ground of their materials, no steeper than their maximum.
// Seaweed: only in the ocean between its depths.
void testCanGrow() {
    const VegetationField field(kSeed);
    const auto& params = field.params();
    for (int t = 0; t < terrain::kPlantTypeCount; ++t) {
        const PlantType type = typeAt(t);
        const float maxSlope = params.rules[slot(type)].maxSlope;
        const ClimateSample gentle = climateOf(0.0f);
        if (type == PlantType::Seaweed) {
            const auto water = SurfaceMaterial::Water;
            TEST_CHECK(field.canGrow(type, water, gentle, oceanSite(10.0f)));
            TEST_CHECK(!field.canGrow(type, water, gentle, oceanSite(0.5f)));
            TEST_CHECK(
                !field.canGrow(type, water, gentle, oceanSite(params.maxSeaweedDepth + 1.0f)));
            TEST_CHECK(!field.canGrow(type, water, climateOf(maxSlope + 0.1f), oceanSite(10.0f)));
            TEST_CHECK(!field.canGrow(type, water, gentle, riverSite()));
            TEST_CHECK(!field.canGrow(type, SurfaceMaterial::Sand, gentle, landSite()));
            continue;
        }
        bool somewhere = false;
        for (int m = 0; m < terrain::kSurfaceMaterialCount; ++m) {
            const auto material = static_cast<SurfaceMaterial>(m);
            const bool allowed =
                (params.rules[slot(type)].materials & terrain::materialBit(material)) != 0;
            TEST_CHECK(field.canGrow(type, material, gentle, landSite()) == allowed);
            somewhere = somewhere || allowed;
            if (!allowed) {
                continue;
            }
            TEST_CHECK(field.canGrow(type, material, climateOf(maxSlope), landSite()));
            TEST_CHECK(!field.canGrow(type, material, climateOf(maxSlope + 0.01f), landSite()));
            TEST_CHECK(!field.canGrow(type, material, gentle, landSite(maxSlope + 0.01f)));
            TEST_CHECK(!field.canGrow(type, material, gentle, oceanSite(5.0f)));
            TEST_CHECK(!field.canGrow(type, material, gentle, riverSite()));
            SurfaceSite wall = landSite();
            wall.cliffWall = true;
            TEST_CHECK(!field.canGrow(type, material, gentle, wall));
            SurfaceSite arch = landSite();
            arch.rockAboveAir = true;
            TEST_CHECK(!field.canGrow(type, material, gentle, arch));
        }
        TEST_CHECK(somewhere);
    }
}

// Densities are within [0, 1] with a sum of at most 1, and 0 wherever the type cannot grow.
void testDensities() {
    const VegetationField field(kSeed);
    const std::vector<SurfaceSite> sites = {landSite(),       landSite(0.4f),   landSite(0.95f),
                                            oceanSite(10.0f), oceanSite(60.0f), riverSite()};
    bool valid = true;
    bool someForest = false;
    for (int b = 0; b < terrain::kBiomeCount; ++b) {
        for (int m = 0; m < terrain::kSurfaceMaterialCount; ++m) {
            for (const SurfaceSite& site : sites) {
                for (const float slope : {0.0f, 0.3f, 0.65f}) {
                    const ClimateSample climate = climateOf(slope);
                    const auto material = static_cast<SurfaceMaterial>(m);
                    const PlantDensities d = field.densities(
                        1000.0 * b, -700.0 * m, static_cast<Biome>(b), material, climate, site);
                    valid = valid && terrain::totalDensity(d) <= 1.0f + 1e-6f;
                    for (int t = 0; t < terrain::kPlantTypeCount; ++t) {
                        const float density = d[static_cast<std::size_t>(t)];
                        valid =
                            valid && density >= 0.0f &&
                            (density == 0.0f || field.canGrow(typeAt(t), material, climate, site));
                    }
                    someForest = someForest || d[slot(PlantType::Tree)] > 0.3f;
                }
            }
        }
    }
    TEST_CHECK(valid);
    TEST_CHECK(someForest);
    // Biome by biome: cacti only in dry biomes, seaweed only in the ocean, nothing on snow.
    const PlantDensities forest = field.densities(
        0.0, 0.0, Biome::TemperateForest, SurfaceMaterial::Grass, climateOf(0.0f), landSite());
    TEST_CHECK(forest[slot(PlantType::Tree)] > 0.0f && forest[slot(PlantType::Cactus)] == 0.0f);
    const PlantDensities desert = field.densities(0.0, 0.0, Biome::Desert, SurfaceMaterial::Sand,
                                                  climateOf(0.0f), landSite());
    TEST_CHECK(desert[slot(PlantType::Cactus)] > 0.0f && desert[slot(PlantType::Tree)] == 0.0f);
    const PlantDensities sea = field.densities(0.0, 0.0, Biome::Ocean, SurfaceMaterial::Water,
                                               climateOf(0.0f), oceanSite(10.0f));
    TEST_CHECK(sea[slot(PlantType::Seaweed)] > 0.0f &&
               terrain::totalDensity(sea) == sea[slot(PlantType::Seaweed)]);
    const PlantDensities snow =
        field.densities(0.0, 0.0, Biome::Snow, SurfaceMaterial::Snow, climateOf(0.0f), landSite());
    TEST_CHECK(terrain::totalDensity(snow) == 0.0f);
}

// Clumps are in [0, 1] and vary: woods and glades.
void testClumps() {
    const VegetationField field(kSeed);
    float low = 1.0f;
    float high = 0.0f;
    for (int i = 0; i < 400; ++i) {
        const float clump = field.clumpAt(37.0 * i, -53.0 * i);
        TEST_CHECK(clump >= 0.0f && clump <= 1.0f);
        low = std::min(low, clump);
        high = std::max(high, clump);
    }
    TEST_CHECK(low < 0.25f && high > 0.75f);
}

// Draws are deterministic, inside their cell and within their ranges, and change with the
// seed.
void testDraws() {
    const VegetationField a(kSeed);
    const VegetationField b(kSeed);
    const VegetationField other(kSeed + 1);
    const auto& params = a.params();
    int differences = 0;
    bool inside = true;
    for (std::int64_t k = -20; k < 20; ++k) {
        for (std::int64_t i = -20; i < 20; ++i) {
            const LatticeDraw draw = a.drawCell(i, k);
            const LatticeDraw same = b.drawCell(i, k);
            TEST_CHECK(draw.x == same.x && draw.z == same.z && draw.pick == same.pick &&
                       draw.scale == same.scale && draw.rotation == same.rotation);
            const double x0 = static_cast<double>(i) * params.cellSize;
            const double z0 = static_cast<double>(k) * params.cellSize;
            inside = inside && draw.x >= x0 && draw.x < x0 + params.cellSize && draw.z >= z0 &&
                     draw.z < z0 + params.cellSize && draw.pick >= 0.0 && draw.pick < 1.0 &&
                     draw.scale >= params.minScale && draw.scale <= params.maxScale &&
                     draw.rotation >= 0.0f &&
                     draw.rotation <= static_cast<float>(2.0 * std::numbers::pi);
            if (other.drawCell(i, k).pick != draw.pick) {
                ++differences;
            }
        }
    }
    TEST_CHECK(inside);
    TEST_CHECK(differences > 1590);
}

void testPickPlant() {
    PlantDensities densities{};
    densities[slot(PlantType::Tree)] = 0.25f;
    densities[slot(PlantType::Flower)] = 0.25f;
    PlantType type = PlantType::Seaweed;
    TEST_CHECK(terrain::pickPlant(densities, 0.1, type) && type == PlantType::Tree);
    TEST_CHECK(terrain::pickPlant(densities, 0.3, type) && type == PlantType::Flower);
    TEST_CHECK(!terrain::pickPlant(densities, 0.5, type));
    TEST_CHECK(!terrain::pickPlant(PlantDensities{}, 0.0, type));
    TEST_CHECK(terrain::totalDensity(densities) == 0.5f);
}

} // namespace

int main() {
    testNamesAndTables();
    testCanGrow();
    testDensities();
    testClumps();
    testDraws();
    testPickPlant();
    return test::result();
}
