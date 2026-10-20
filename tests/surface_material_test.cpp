// Surface materials: water exactly on water, rock on steep ground, walls and rock above air,
// the rule of each material over a sweep of biomes, climates and surfaces.
#include "terrain/generation/surface_material.hpp"

#include "test_assert.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace {

using terrain::Biome;
using terrain::ClimateSample;
using terrain::MaterialParams;
using terrain::SurfaceMaterial;
using terrain::SurfaceSite;
using terrain::WaterKind;

ClimateSample climateOf(float temperature, float slope) {
    ClimateSample climate;
    climate.baseTemperature = temperature;
    climate.temperature = temperature;
    climate.humidity = 0.5f;
    climate.slope = slope;
    return climate;
}

SurfaceSite siteOf(WaterKind water, float steepness = 0.0f,
                   float riverDistance = terrain::kNoRiverDistance) {
    SurfaceSite site;
    site.ground = water == WaterKind::Ocean ? -20.0f : 300.0f;
    site.water = water;
    site.waterLevel = water == WaterKind::Ocean ? terrain::kSeaLevel : site.ground;
    site.steepness = steepness;
    site.riverDistance = water == WaterKind::River ? 0.0f : riverDistance;
    return site;
}

Biome biomeAt(int index) {
    return static_cast<Biome>(index);
}

void testNames() {
    for (int a = 0; a < terrain::kSurfaceMaterialCount; ++a) {
        const char* name = terrain::materialName(static_cast<SurfaceMaterial>(a));
        TEST_CHECK(name != nullptr && std::strlen(name) > 0);
        for (int b = 0; b < a; ++b) {
            TEST_CHECK(std::strcmp(name, terrain::materialName(static_cast<SurfaceMaterial>(b))) !=
                       0);
        }
    }
    TEST_CHECK(std::strcmp(terrain::materialName(SurfaceMaterial::Pebbles), "pebbles") == 0);
}

void testSteepestSlope() {
    SurfaceSite site = siteOf(WaterKind::None, 0.4f);
    TEST_CHECK(terrain::steepestSlope(climateOf(10.0f, 0.2f), site) == 0.4f);
    TEST_CHECK(terrain::steepestSlope(climateOf(10.0f, 0.7f), site) == 0.7f);
}

// Water on every ocean and river column, whatever the biome; never on dry land.
void testWater() {
    bool water = true;
    for (int b = 0; b < terrain::kBiomeCount; ++b) {
        for (const float slope : {0.0f, 0.5f, 3.0f}) {
            const ClimateSample climate = climateOf(10.0f, slope);
            water =
                water && terrain::surfaceMaterial(biomeAt(b), climate, siteOf(WaterKind::Ocean)) ==
                             SurfaceMaterial::Water;
            water =
                water && terrain::surfaceMaterial(biomeAt(b), climate, siteOf(WaterKind::River)) ==
                             SurfaceMaterial::Water;
            water =
                water && terrain::surfaceMaterial(biomeAt(b), climate, siteOf(WaterKind::None)) !=
                             SurfaceMaterial::Water;
        }
    }
    TEST_CHECK(water);
}

// Bare rock on steep dry ground, at the walls of the cliffs and on rock above air, whatever
// the biome.
void testRock() {
    const MaterialParams params;
    bool rock = true;
    for (int b = 0; b < terrain::kBiomeCount; ++b) {
        const ClimateSample gentle = climateOf(10.0f, 0.1f);
        rock = rock && terrain::surfaceMaterial(biomeAt(b), climateOf(10.0f, params.rockSlope),
                                                siteOf(WaterKind::None)) == SurfaceMaterial::Rock;
        rock =
            rock && terrain::surfaceMaterial(biomeAt(b), gentle, siteOf(WaterKind::None, 2.0f)) ==
                        SurfaceMaterial::Rock;
        SurfaceSite wall = siteOf(WaterKind::None);
        wall.cliffWall = true;
        rock = rock && terrain::surfaceMaterial(biomeAt(b), gentle, wall) == SurfaceMaterial::Rock;
        SurfaceSite arch = siteOf(WaterKind::None);
        arch.rockAboveAir = true;
        rock = rock && terrain::surfaceMaterial(biomeAt(b), gentle, arch) == SurfaceMaterial::Rock;
    }
    TEST_CHECK(rock);
}

// The rule a dry column must satisfy to be given each material.
bool satisfiesRule(SurfaceMaterial material, Biome biome, const ClimateSample& c,
                   const SurfaceSite& site, const MaterialParams& p) {
    const float steepest = terrain::steepestSlope(c, site);
    const float t = c.temperature;
    const bool bank = site.riverDistance <= p.riverBankWidth;
    switch (material) {
    case SurfaceMaterial::Water:
        return !site.dry();
    case SurfaceMaterial::Rock:
        return steepest >= p.rockSlope || site.cliffWall || site.rockAboveAir ||
               (biome == Biome::Mountain && steepest < p.screeSlope);
    case SurfaceMaterial::Snow:
        return biome == Biome::Snow || (biome == Biome::Mountain && t <= p.mountainSnowTemperature);
    case SurfaceMaterial::Sand:
        return biome == Biome::Desert || (biome == Biome::Beach && t >= p.pebbleBeachTemperature);
    case SurfaceMaterial::Mud:
        return biome == Biome::Marsh;
    case SurfaceMaterial::Pebbles:
        return (biome == Biome::Mountain && steepest >= p.screeSlope) ||
               (biome == Biome::Beach && t < p.pebbleBeachTemperature) || bank;
    case SurfaceMaterial::Dirt:
        return biome == Biome::Tundra || steepest >= p.dirtSlope;
    case SurfaceMaterial::Grass:
        return steepest < p.dirtSlope && biome != Biome::Tundra && biome != Biome::Desert &&
               biome != Biome::Beach && biome != Biome::Marsh && biome != Biome::Snow &&
               biome != Biome::Mountain && !bank;
    }
    return false;
}

// Over a sweep of biomes, temperatures, slopes and river distances, every dry column satisfies
// the rule of its material, and every material shows up.
void testRulesHold() {
    const MaterialParams params;
    const std::vector<float> slopes = {0.0f, 0.2f, 0.4f, 0.5f, 0.6f, 0.9f, 1.0f, 1.5f};
    const std::vector<float> distances = {0.0f, 10.0f, 30.0f, terrain::kNoRiverDistance};
    std::array<int, terrain::kSurfaceMaterialCount> counts{};
    bool rules = true;
    for (int b = 0; b < terrain::kBiomeCount; ++b) {
        for (float t = -20.0f; t <= 35.0f; t += 1.0f) {
            for (const float slope : slopes) {
                for (const float distance : distances) {
                    for (const WaterKind water : {WaterKind::None, WaterKind::River}) {
                        const ClimateSample climate = climateOf(t, slope);
                        const SurfaceSite site = siteOf(water, 0.0f, distance);
                        const SurfaceMaterial material =
                            terrain::surfaceMaterial(biomeAt(b), climate, site, params);
                        ++counts[static_cast<std::size_t>(material)];
                        rules = rules && satisfiesRule(material, biomeAt(b), climate, site, params);
                    }
                }
            }
        }
    }
    TEST_CHECK(rules);
    for (const int count : counts) {
        TEST_CHECK(count > 0);
    }
}

// A few representative columns.
void testExamples() {
    const SurfaceSite flat = siteOf(WaterKind::None);
    TEST_CHECK(terrain::surfaceMaterial(Biome::Beach, climateOf(20.0f, 0.02f), flat) ==
               SurfaceMaterial::Sand);
    TEST_CHECK(terrain::surfaceMaterial(Biome::Beach, climateOf(0.0f, 0.02f), flat) ==
               SurfaceMaterial::Pebbles);
    TEST_CHECK(terrain::surfaceMaterial(Biome::Marsh, climateOf(15.0f, 0.01f), flat) ==
               SurfaceMaterial::Mud);
    TEST_CHECK(terrain::surfaceMaterial(Biome::Grassland, climateOf(15.0f, 0.1f), flat) ==
               SurfaceMaterial::Grass);
    TEST_CHECK(terrain::surfaceMaterial(Biome::Grassland, climateOf(15.0f, 0.6f), flat) ==
               SurfaceMaterial::Dirt);
    TEST_CHECK(terrain::surfaceMaterial(Biome::TemperateForest, climateOf(15.0f, 0.1f),
                                        siteOf(WaterKind::None, 0.0f, 10.0f)) ==
               SurfaceMaterial::Pebbles);
    TEST_CHECK(terrain::surfaceMaterial(Biome::Mountain, climateOf(0.0f, 0.2f), flat) ==
               SurfaceMaterial::Rock);
    TEST_CHECK(terrain::surfaceMaterial(Biome::Mountain, climateOf(0.0f, 0.6f), flat) ==
               SurfaceMaterial::Pebbles);
    TEST_CHECK(terrain::surfaceMaterial(Biome::Mountain, climateOf(-8.0f, 0.2f), flat) ==
               SurfaceMaterial::Snow);
    TEST_CHECK(terrain::surfaceMaterial(Biome::Snow, climateOf(-15.0f, 0.2f), flat) ==
               SurfaceMaterial::Snow);
}

} // namespace

int main() {
    testNames();
    testSteepestSlope();
    testWater();
    testRock();
    testRulesHold();
    testExamples();
    return test::result();
}
