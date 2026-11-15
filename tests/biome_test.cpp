// Biomes: every biome reachable, the rule of each biome holds over a sweep of climates and
// surfaces, the ocean exactly under the sea, the altitude zonation of the mountains.
#include "terrain/generation/biome.hpp"

#include "test_assert.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace {

using terrain::Biome;
using terrain::BiomeParams;
using terrain::ClimateSample;
using terrain::SurfaceSite;
using terrain::WaterKind;

ClimateSample climateOf(float temperature, float humidity, float slope,
                        float baseTemperature = 15.0f) {
    ClimateSample climate;
    climate.baseTemperature = baseTemperature;
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

// Land coast meters from the coast (the coast signal of the geography, coastDistance).
SurfaceSite land(float altitude, float riverDistance = terrain::kNoRiverDistance,
                 float coast = 1000.0f) {
    SurfaceSite site = siteOf(altitude, WaterKind::None, riverDistance);
    site.continentalness = static_cast<float>(coast / terrain::kContinentalMeters);
    return site;
}

SurfaceSite erg(float altitude, float mask) {
    SurfaceSite site = land(altitude, terrain::kNoRiverDistance, 600'000.0f);
    site.erg = mask;
    return site;
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
    constexpr float kInland = 600'000.0f; // meters from the coast
    const std::array<Case, terrain::kBiomeCount> cases = {{
        {Biome::Ocean, climateOf(15.0f, 0.5f, 0.0f), siteOf(-50.0f, WaterKind::Ocean)},
        {Biome::Beach, climateOf(15.0f, 0.5f, 0.05f), land(2.0f)},
        {Biome::RockyDesert, climateOf(25.0f, 0.1f, 0.05f), land(300.0f)},
        {Biome::Savanna, climateOf(25.0f, 0.45f, 0.05f), land(300.0f)},
        {Biome::Grassland, climateOf(10.0f, 0.35f, 0.05f), land(300.0f)},
        {Biome::TemperateForest, climateOf(12.0f, 0.6f, 0.05f), land(300.0f)},
        {Biome::TropicalForest, climateOf(26.0f, 0.8f, 0.05f), land(300.0f)},
        {Biome::Taiga, climateOf(1.0f, 0.5f, 0.05f), land(300.0f)},
        {Biome::Tundra, climateOf(-4.0f, 0.5f, 0.05f), land(300.0f)},
        {Biome::Mountain, climateOf(-2.0f, 0.5f, 0.3f), land(2500.0f)},
        {Biome::Snow, climateOf(-10.0f, 0.5f, 0.3f), land(3500.0f)},
        {Biome::Marsh, climateOf(15.0f, 0.8f, 0.01f), land(50.0f, 100.0f, kInland)},
        {Biome::DryForest, climateOf(25.0f, 0.55f, 0.05f), land(300.0f)},
        {Biome::Steppe, climateOf(12.0f, 0.25f, 0.05f), land(300.0f)},
        {Biome::SandDesert, climateOf(25.0f, 0.1f, 0.05f), erg(300.0f, 0.9f)},
        {Biome::ColdDesert, climateOf(8.0f, 0.1f, 0.05f), land(300.0f)},
        {Biome::HighMountain, climateOf(-5.0f, 0.5f, 1.0f), land(4000.0f)},
        {Biome::Polar, climateOf(-10.0f, 0.15f, 0.05f, -12.0f), land(100.0f)},
        {Biome::IceDesert, climateOf(-20.0f, 0.5f, 0.05f, -20.0f), land(1500.0f)},
        {Biome::Mediterranean, climateOf(16.0f, 0.4f, 0.05f), land(300.0f)},
        {Biome::Shrubland, climateOf(25.0f, 0.33f, 0.05f), land(300.0f)},
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

// The rule a column must satisfy to be given each biome (agent 49: the cold thresholds follow
// snowTemperature, never a fixed -6).
bool coldRule(Biome biome, const ClimateSample& c, const SurfaceSite& site, const BiomeParams& p) {
    const float altitude = site.ground - terrain::kSeaLevel;
    const float snowT = p.snowTemperature;
    const bool polarCold = c.baseTemperature <= snowT + p.polarBaseOffset;
    const bool polarDry = polarCold && c.humidity < p.polarDryHumidity;
    const bool sheet = c.baseTemperature <= snowT + p.iceDesertBaseOffset;
    const bool snowy = c.temperature <= snowT && c.slope <= p.snowMaxSlope && !polarDry;
    switch (biome) {
    case Biome::Snow:
        return snowy && !sheet;
    case Biome::IceDesert:
        return snowy && sheet;
    case Biome::Polar:
        return polarCold && ((c.temperature <= snowT && polarDry) ||
                             c.temperature < p.tundraTemperature);
    case Biome::HighMountain:
        return altitude >= p.mountainMinAltitude && c.temperature <= snowT + p.highMountainOffset;
    case Biome::Mountain:
        return altitude >= p.mountainMinAltitude &&
               (c.temperature <= p.treelineTemperature ||
                (c.slope >= p.mountainSlope && !site.cliffWall));
    default:
        return false;
    }
}

bool temperateRule(Biome biome, float t, float h, const BiomeParams& p) {
    const bool temperate = t >= p.taigaTemperature && t < p.tropicalTemperature;
    const bool hot = t >= p.tropicalTemperature;
    switch (biome) {
    case Biome::RockyDesert:
        return (temperate && t >= p.coldDesertTemperature && h < p.temperateDesertHumidity) ||
               (hot && h < p.hotDesertHumidity);
    case Biome::ColdDesert:
        return (t < p.taigaTemperature && h < p.coldDesertHumidity) ||
               (temperate && t < p.coldDesertTemperature && h < p.temperateDesertHumidity);
    case Biome::Steppe:
        return temperate && h >= p.temperateDesertHumidity && h < p.steppeHumidity;
    case Biome::Grassland:
        return temperate && h >= p.steppeHumidity && h < p.forestHumidity &&
               t < p.mediterraneanTemperature;
    case Biome::Mediterranean:
        return temperate && h >= p.steppeHumidity && h < p.forestHumidity &&
               t >= p.mediterraneanTemperature;
    case Biome::TemperateForest:
        return temperate && h >= p.forestHumidity;
    case Biome::Shrubland:
        return hot && h >= p.hotDesertHumidity && h < p.shrublandHumidity;
    case Biome::Savanna:
        return hot && h >= p.shrublandHumidity && h < p.dryForestHumidity;
    case Biome::DryForest:
        return hot && h >= p.dryForestHumidity && h < p.rainforestHumidity;
    case Biome::TropicalForest:
        return hot && h >= p.rainforestHumidity;
    case Biome::Taiga:
        return t >= p.tundraTemperature && t < p.taigaTemperature && h >= p.taigaMinHumidity;
    case Biome::Tundra:
        return t < p.taigaTemperature && h >= p.coldDesertHumidity &&
               (t < p.tundraTemperature || h < p.taigaMinHumidity);
    default:
        return false;
    }
}

bool satisfiesRule(Biome biome, const ClimateSample& c, const SurfaceSite& site,
                   const BiomeParams& p) {
    const float altitude = site.ground - terrain::kSeaLevel;
    const bool land = site.water != WaterKind::Ocean;
    const float coast = terrain::coastDistance(site);
    switch (biome) {
    case Biome::Ocean:
        return site.water == WaterKind::Ocean;
    case Biome::Beach:
        return land && altitude >= 0.0f && altitude <= p.beachMaxHeight &&
               c.slope <= p.beachMaxSlope && coast <= p.beachCoastReach;
    case Biome::Marsh:
        return land && c.slope <= p.marshMaxSlope && altitude <= p.marshMaxAltitude &&
               c.humidity >= p.marshMinHumidity && c.temperature >= p.marshMinTemperature &&
               (site.riverDistance <= p.marshRiverReach ||
                (altitude <= p.marshCoastHeight && coast <= p.marshCoastReach));
    case Biome::SandDesert:
        return land && site.erg >= p.sandDesertErg;
    case Biome::Snow:
    case Biome::IceDesert:
    case Biome::Polar:
    case Biome::HighMountain:
    case Biome::Mountain:
        return land && coldRule(biome, c, site, p);
    default:
        return land && temperateRule(biome, c.temperature, c.humidity, p);
    }
}

struct Surroundings {
    float riverDistance;
    bool wall;
    float coast; // meters from the coast
    float erg;
};

// Over a sweep of climates and surfaces: every column satisfies the rule of its biome, the
// ocean is exactly the columns under the sea, and very cold gentle land (not too dry where the
// latitude makes it cold) is always snow or ice desert.
void testRulesHold() {
    const BiomeParams params;
    const std::vector<float> altitudes = {-30.0f, 0.5f,   3.0f,    10.0f,   100.0f,  250.0f,
                                          600.0f, 1100.0f, 1300.0f, 2000.0f, 3500.0f};
    const std::vector<float> slopes = {0.0f, 0.02f, 0.05f, 0.2f, 0.5f, 0.75f, 1.2f};
    const std::vector<Surroundings> surroundings = {
        {0.0f, false, 0.0f, 0.0f},        {200.0f, false, 500.0f, 0.0f},
        {terrain::kNoRiverDistance, false, 0.0f, 0.0f}, {200.0f, true, 0.0f, 0.0f},
        {terrain::kNoRiverDistance, false, 600'000.0f, 0.0f},
        {terrain::kNoRiverDistance, false, 600'000.0f, 0.9f}};
    const std::array<WaterKind, 3> waters = {WaterKind::None, WaterKind::River, WaterKind::Ocean};
    bool rules = true;
    bool ocean = true;
    bool snow = true;
    std::array<int, terrain::kBiomeCount> counts{};
    for (const float base : {15.0f, -8.0f, -15.0f}) {
        for (float t = -30.0f; t <= 40.0f; t += 2.5f) {
            for (int hi = 0; hi <= 20; ++hi) {
                const float h = 0.05f * static_cast<float>(hi);
                for (const float altitude : altitudes) {
                    for (const float slope : slopes) {
                        for (const Surroundings& around : surroundings) {
                            for (const WaterKind water : waters) {
                                const float ground =
                                    water == WaterKind::Ocean ? -std::abs(altitude) : altitude;
                                SurfaceSite site = siteOf(ground, water, around.riverDistance);
                                site.cliffWall = around.wall;
                                site.continentalness = static_cast<float>(
                                    around.coast / terrain::kContinentalMeters);
                                site.erg = around.erg;
                                const ClimateSample climate = climateOf(t, h, slope, base);
                                const Biome biome = terrain::classifyBiome(climate, site, params);
                                ++counts[static_cast<std::size_t>(biome)];
                                rules = rules && satisfiesRule(biome, climate, site, params);
                                ocean = ocean &&
                                        ((biome == Biome::Ocean) == (water == WaterKind::Ocean));
                                const bool polarDry =
                                    base <= params.snowTemperature + params.polarBaseOffset &&
                                    h < params.polarDryHumidity;
                                if (water != WaterKind::Ocean && t <= params.snowTemperature &&
                                    slope <= params.snowMaxSlope && !polarDry) {
                                    snow = snow &&
                                           (biome == Biome::Snow || biome == Biome::IceDesert);
                                }
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

// A dry basin far from the sea is neither beach nor marsh (agent 52: the basins and trenches below
// the sea level); the same low ground near the coast is, and so is an island in the sea of the
// coast signal (agent 51).
void testBeachAndMarshNearTheSea() {
    const ClimateSample mild = climateOf(15.0f, 0.5f, 0.02f);
    const ClimateSample humid = climateOf(15.0f, 0.8f, 0.01f);
    TEST_CHECK(terrain::classifyBiome(mild, land(2.0f, terrain::kNoRiverDistance, 300'000.0f)) !=
               Biome::Beach);
    TEST_CHECK(terrain::classifyBiome(mild, land(2.0f, terrain::kNoRiverDistance, 1000.0f)) ==
               Biome::Beach);
    TEST_CHECK(terrain::classifyBiome(mild, land(2.0f, terrain::kNoRiverDistance, -50'000.0f)) ==
               Biome::Beach);
    TEST_CHECK(terrain::classifyBiome(humid, land(-20.0f, terrain::kNoRiverDistance, 300'000.0f)) !=
               Biome::Marsh);
    TEST_CHECK(terrain::classifyBiome(humid, land(10.0f, terrain::kNoRiverDistance, 2000.0f)) ==
               Biome::Marsh);
    TEST_CHECK(terrain::classifyBiome(humid, land(10.0f, 100.0f, 300'000.0f)) == Biome::Marsh);
}

// The cold biomes follow snowTemperature (agent 31 will set one threshold for the snow and the
// frost): moving it moves them.
void testColdFollowsTheSnow() {
    BiomeParams warmer;
    warmer.snowTemperature = -2.0f;
    const ClimateSample cold = climateOf(-4.0f, 0.5f, 0.05f, -9.0f);
    TEST_CHECK(terrain::classifyBiome(cold, land(100.0f)) != Biome::IceDesert);
    TEST_CHECK(terrain::classifyBiome(cold, land(100.0f), warmer) == Biome::IceDesert);
    const ClimateSample alpine = climateOf(-1.5f, 0.5f, 0.3f);
    TEST_CHECK(terrain::classifyBiome(alpine, land(3000.0f)) == Biome::Mountain);
    TEST_CHECK(terrain::classifyBiome(alpine, land(3000.0f), warmer) == Biome::HighMountain);
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

// Rank of a biome in the altitude zonation: lowlands, cold lowlands, mountain, high mountain,
// snow.
int zone(Biome biome) {
    switch (biome) {
    case Biome::Taiga:
    case Biome::Tundra:
        return 1;
    case Biome::Mountain:
        return 2;
    case Biome::HighMountain:
        return 3;
    case Biome::Snow:
        return 4;
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
        TEST_CHECK(seen[static_cast<std::size_t>(Biome::HighMountain)]);
        TEST_CHECK(seen[static_cast<std::size_t>(Biome::Snow)]);
        if (base == 14.0f) {
            TEST_CHECK(seen[static_cast<std::size_t>(Biome::TemperateForest)]);
            TEST_CHECK(seen[static_cast<std::size_t>(Biome::Mountain)]);
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
    testBeachAndMarshNearTheSea();
    testColdFollowsTheSnow();
    testCliffWallsKeepTheirBiome();
    testAltitudeZonation();
    return test::result();
}
