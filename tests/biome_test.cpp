// Biomes: every biome reachable, the rule of each biome holds over a sweep of climates and
// surfaces, the ocean exactly under the sea, the altitude zonation of the mountains.
#include "terrain/generation/biome.hpp"

#include "test_assert.hpp"

#include <array>
#include <cstring>
#include <utility>
#include <vector>

namespace {

using terrain::Biome;
using terrain::BiomeParams;
using terrain::ClimateSample;
using terrain::SurfaceSite;
using terrain::WaterKind;

ClimateSample climateOf(float temperature, float humidity, float slope) {
    ClimateSample climate;
    climate.baseTemperature = temperature;
    climate.temperature = temperature;
    climate.humidity = humidity;
    climate.slope = slope;
    return climate;
}

SurfaceSite siteOf(float ground, WaterKind water, float riverDistance = terrain::kNoRiverDistance) {
    SurfaceSite site;
    site.ground = ground;
    site.water = water;
    site.waterLevel = water == WaterKind::Ocean ? terrain::kSeaLevel : ground;
    site.riverDistance = water == WaterKind::River ? 0.0f : riverDistance;
    return site;
}

SurfaceSite land(float altitude, float riverDistance = terrain::kNoRiverDistance) {
    return siteOf(altitude, WaterKind::None, riverDistance);
}

void testNames() {
    for (int a = 0; a < terrain::kBiomeCount; ++a) {
        const char* name = terrain::biomeName(static_cast<Biome>(a));
        TEST_CHECK(name != nullptr && std::strlen(name) > 0);
        for (int b = 0; b < a; ++b) {
            TEST_CHECK(std::strcmp(name, terrain::biomeName(static_cast<Biome>(b))) != 0);
        }
    }
    TEST_CHECK(std::strcmp(terrain::biomeName(Biome::TemperateForest), "temperate forest") == 0);
}

void testEveryBiomeReachable() {
    struct Case {
        Biome expected;
        ClimateSample climate;
        SurfaceSite site;
    };
    const std::array<Case, terrain::kBiomeCount> cases = {{
        {Biome::Ocean, climateOf(15.0f, 0.5f, 0.0f), siteOf(-50.0f, WaterKind::Ocean)},
        {Biome::Beach, climateOf(15.0f, 0.5f, 0.05f), land(2.0f)},
        {Biome::Desert, climateOf(15.0f, 0.1f, 0.05f), land(300.0f)},
        {Biome::Savanna, climateOf(25.0f, 0.45f, 0.05f), land(300.0f)},
        {Biome::Grassland, climateOf(12.0f, 0.35f, 0.05f), land(300.0f)},
        {Biome::TemperateForest, climateOf(12.0f, 0.6f, 0.05f), land(300.0f)},
        {Biome::TropicalForest, climateOf(26.0f, 0.8f, 0.05f), land(300.0f)},
        {Biome::Taiga, climateOf(1.0f, 0.5f, 0.05f), land(300.0f)},
        {Biome::Tundra, climateOf(-4.0f, 0.5f, 0.05f), land(300.0f)},
        {Biome::Mountain, climateOf(-3.0f, 0.5f, 0.3f), land(2500.0f)},
        {Biome::Snow, climateOf(-10.0f, 0.5f, 0.3f), land(3500.0f)},
        {Biome::Marsh, climateOf(15.0f, 0.8f, 0.01f), land(50.0f, 100.0f)},
    }};
    std::array<bool, terrain::kBiomeCount> seen{};
    for (const Case& c : cases) {
        const Biome biome = terrain::classifyBiome(c.climate, c.site);
        TEST_CHECK(biome == c.expected);
        seen[static_cast<std::size_t>(biome)] = true;
    }
    for (const bool s : seen) {
        TEST_CHECK(s);
    }
}

// The rule a column must satisfy to be given each biome.
bool satisfiesRule(Biome biome, const ClimateSample& c, const SurfaceSite& site,
                   const BiomeParams& p) {
    const float altitude = site.ground - terrain::kSeaLevel;
    const float t = c.temperature;
    const float h = c.humidity;
    const bool land = site.water != WaterKind::Ocean;
    switch (biome) {
    case Biome::Ocean:
        return site.water == WaterKind::Ocean;
    case Biome::Snow:
        return land && t <= p.snowTemperature && c.slope <= p.snowMaxSlope;
    case Biome::Mountain:
        return land && altitude >= p.mountainMinAltitude &&
               (t <= p.treelineTemperature || (c.slope >= p.mountainSlope && !site.cliffWall));
    case Biome::Beach:
        return land && altitude <= p.beachMaxHeight && c.slope <= p.beachMaxSlope;
    case Biome::Marsh:
        return land && c.slope <= p.marshMaxSlope && altitude <= p.marshMaxAltitude &&
               h >= p.marshMinHumidity && t >= p.marshMinTemperature &&
               (site.riverDistance <= p.marshRiverReach || altitude <= p.marshCoastHeight);
    case Biome::Desert:
        return land && t >= p.taigaTemperature && h < p.hotDesertHumidity;
    case Biome::Savanna:
        return land && t >= p.tropicalTemperature && h >= p.hotDesertHumidity &&
               h < p.rainforestHumidity;
    case Biome::TropicalForest:
        return land && t >= p.tropicalTemperature && h >= p.rainforestHumidity;
    case Biome::Grassland:
        return land && t >= p.taigaTemperature && t < p.tropicalTemperature &&
               h >= p.temperateDesertHumidity && h < p.forestHumidity;
    case Biome::TemperateForest:
        return land && t >= p.taigaTemperature && t < p.tropicalTemperature &&
               h >= p.forestHumidity;
    case Biome::Taiga:
        return land && t >= p.tundraTemperature && t < p.taigaTemperature &&
               h >= p.taigaMinHumidity;
    case Biome::Tundra:
        return land && t < p.taigaTemperature;
    }
    return false;
}

// Over a sweep of climates and surfaces: every column satisfies the rule of its biome, the
// ocean is exactly the columns under the sea, and very cold gentle land is always snow.
void testRulesHold() {
    const BiomeParams params;
    const std::vector<float> altitudes = {0.5f,   3.0f,    10.0f,   100.0f,  250.0f,
                                          600.0f, 1100.0f, 1300.0f, 2000.0f, 3500.0f};
    const std::vector<float> slopes = {0.0f, 0.02f, 0.05f, 0.2f, 0.5f, 0.75f, 1.2f};
    // River distance, and whether the column is a wall of the cliffs.
    const std::vector<std::pair<float, bool>> surroundings = {
        {0.0f, false}, {200.0f, false}, {terrain::kNoRiverDistance, false}, {200.0f, true}};
    const std::array<WaterKind, 3> waters = {WaterKind::None, WaterKind::River, WaterKind::Ocean};
    bool rules = true;
    bool ocean = true;
    bool snow = true;
    std::array<int, terrain::kBiomeCount> counts{};
    for (float t = -30.0f; t <= 40.0f; t += 2.5f) {
        for (int hi = 0; hi <= 20; ++hi) {
            const float h = 0.05f * static_cast<float>(hi);
            for (const float altitude : altitudes) {
                for (const float slope : slopes) {
                    for (const auto& [distance, wall] : surroundings) {
                        for (const WaterKind water : waters) {
                            const float ground = water == WaterKind::Ocean ? -altitude : altitude;
                            SurfaceSite site = siteOf(ground, water, distance);
                            site.cliffWall = wall;
                            const ClimateSample climate = climateOf(t, h, slope);
                            const Biome biome = terrain::classifyBiome(climate, site, params);
                            ++counts[static_cast<std::size_t>(biome)];
                            rules = rules && satisfiesRule(biome, climate, site, params);
                            ocean =
                                ocean && ((biome == Biome::Ocean) == (water == WaterKind::Ocean));
                            if (water != WaterKind::Ocean &&
                                satisfiesRule(Biome::Snow, climate, site, params)) {
                                snow = snow && biome == Biome::Snow;
                            }
                        }
                    }
                }
            }
        }
    }
    TEST_CHECK(rules);
    TEST_CHECK(ocean);
    TEST_CHECK(snow);
    for (const int count : counts) {
        TEST_CHECK(count > 0);
    }
}

// A wall of the cliffs keeps the biome of the land around it; a steep slope elsewhere at the
// same altitude is mountain.
void testCliffWallsKeepTheirBiome() {
    const ClimateSample steep = climateOf(12.0f, 0.6f, 2.5f);
    SurfaceSite wall = land(1500.0f);
    wall.cliffWall = true;
    TEST_CHECK(terrain::classifyBiome(steep, wall) == Biome::TemperateForest);
    TEST_CHECK(terrain::classifyBiome(steep, land(1500.0f)) == Biome::Mountain);
    // Above the tree line, a wall is mountain like the slopes around it.
    TEST_CHECK(terrain::classifyBiome(climateOf(-3.0f, 0.6f, 2.5f), wall) == Biome::Mountain);
}

// Rank of a biome in the altitude zonation: lowlands, cold lowlands, mountain, snow.
int zone(Biome biome) {
    switch (biome) {
    case Biome::Taiga:
    case Biome::Tundra:
        return 1;
    case Biome::Mountain:
        return 2;
    case Biome::Snow:
        return 3;
    default:
        return 0;
    }
}

// Climbing a mountain with the temperature corrected by the altitude: the biomes go up the
// zonation and never back down.
void testAltitudeZonation() {
    const terrain::ClimateField field(terrain::kDefaultWorldSeed);
    for (const float base : {14.0f, 4.0f}) {
        int previous = 0;
        bool monotonic = true;
        std::array<bool, terrain::kBiomeCount> seen{};
        for (float altitude = 0.0f; altitude <= 5000.0f; altitude += 5.0f) {
            const float t = field.temperatureAt(base, altitude, 0.0f);
            const Biome biome = terrain::classifyBiome(climateOf(t, 0.7f, 0.2f), land(altitude));
            seen[static_cast<std::size_t>(biome)] = true;
            monotonic = monotonic && zone(biome) >= previous;
            previous = zone(biome);
        }
        TEST_CHECK(monotonic);
        TEST_CHECK(seen[static_cast<std::size_t>(Biome::Taiga)]);
        TEST_CHECK(seen[static_cast<std::size_t>(Biome::Mountain)]);
        TEST_CHECK(seen[static_cast<std::size_t>(Biome::Snow)]);
        if (base == 14.0f) {
            TEST_CHECK(seen[static_cast<std::size_t>(Biome::TemperateForest)]);
        } else {
            TEST_CHECK(seen[static_cast<std::size_t>(Biome::Tundra)]);
        }
    }
}

} // namespace

int main() {
    testNames();
    testEveryBiomeReachable();
    testRulesHold();
    testCliffWallsKeepTheirBiome();
    testAltitudeZonation();
    return test::result();
}
