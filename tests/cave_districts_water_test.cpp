// Water of the cave districts (Phase 8C-1): the aquifer cells follow the profile of the district
// at their site (wet chance, level, flooded, frozen), the lakes districts hold more water and the
// volcanic ones none, and the core of a flooded district, in the default world, is full of water
// up to just under its roofs and opens no entrance. Entrances and lakes of blended districts:
// cave_districts_lakes_test.cpp; shapes of the caves: cave_districts_carving_test.cpp.
//
// Each profile is measured as the profile of every district (uniformCaves) against the neutral
// one, over flat ground of a mild climate.
#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "aquifers_test_support.hpp"
#include "cave_districts_carving_support.hpp"
#include "caves_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace {

using aquifers_test::climateGround;
using caves_test::kSeed;
using caves_test::Window;
using districts_test::defaultCaves;
using districts_test::defaultProfile;
using districts_test::districtsOf;
using districts_test::forEachSample;
using districts_test::uniformCaves;
using terrain::AquiferCache;
using terrain::AquiferCell;
using terrain::AquiferParams;
using terrain::CaveColumn;
using terrain::CaveDetail;
using terrain::CaveDistrictType;
using terrain::CaveField;
using terrain::CaveGround;
using terrain::CaveProfile;
using terrain::CaveSampler;
using terrain::kCaveDistrictTypeCount;
using terrain::TerrainTiles;

constexpr float kFlatHeight = 300.0f;

CaveDistrictType typeAt(int t) {
    return static_cast<CaveDistrictType>(t);
}

// ---------------------------------------------------------------------------

float lowestGround(const CaveGround& ground, const AquiferParams& p, const AquiferCell& cell) {
    float lowest = ground.height(cell.siteX, cell.siteZ);
    for (int probe = 0; probe < 9; ++probe) {
        lowest = std::min(lowest, ground.height((cell.cellX + 0.5 * (probe % 3)) * p.cellSize,
                                                (cell.cellZ + 0.5 * (probe / 3)) * p.cellSize));
    }
    return lowest;
}

std::vector<AquiferCell> cellsOf(const CaveField& field, const CaveGround& ground, int half) {
    AquiferCache cache(field, ground);
    std::vector<AquiferCell> cells;
    for (int cellZ = -half; cellZ < half; ++cellZ) {
        for (int cellX = -half; cellX < half; ++cellX) {
            cells.push_back(cache.at(cellX, cellZ));
        }
    }
    return cells;
}

// Chance that a cell of the given humidity is wet, as the profile changes it: the wet offset is
// added and clamped, then the flooded share of what is missing to 1.
double expectedChance(const AquiferParams& p, const CaveProfile& profile, float humidity) {
    const float humid = terrain::noise::smoothstep(p.dryHumidity, p.wetHumidity, humidity);
    float chance = p.dryChance + humid * (p.wetChance - p.dryChance);
    chance = std::clamp(chance + profile[CaveProfile::kWetOffset], 0.0f, 1.0f);
    return static_cast<double>(chance + profile[CaveProfile::kFlooded] * (1.0f - chance));
}

struct CellChecks {
    int wet = 0;
    bool supersets = true;   // every cell wet without districts is wet (wetter profiles)
    bool subsets = true;     // every cell wet is wet without districts (drier profiles)
    bool levelsShift = true; // the level of a cell wet in both moves by the rise
    bool frozen = true;      // frozen exactly where wet in an icy profile, nowhere else
    bool flooded = true;     // every cell wet, up to just under the roof of its lowest ground
};

CellChecks checkCells(const AquiferParams& p, const CaveGround& ground, double roof,
                      const CaveProfile& profile, const std::vector<AquiferCell>& neutral,
                      const std::vector<AquiferCell>& cells) {
    CellChecks checks;
    for (std::size_t n = 0; n < cells.size(); ++n) {
        const AquiferCell& cell = cells[n];
        checks.wet += cell.wet ? 1 : 0;
        checks.supersets = checks.supersets && (!neutral[n].wet || cell.wet);
        checks.subsets = checks.subsets && (!cell.wet || neutral[n].wet);
        checks.frozen = checks.frozen && cell.frozen == (cell.wet && profile.icy());
        if (cell.wet && neutral[n].wet && profile[CaveProfile::kFlooded] == 0.0f) {
            checks.levelsShift =
                checks.levelsShift && std::abs(cell.level - neutral[n].level -
                                               profile[CaveProfile::kLevelRise]) < 1.0e-3f;
        }
        if (profile[CaveProfile::kFlooded] == 1.0f) {
            const double level = std::floor(static_cast<double>(lowestGround(ground, p, cell)) -
                                            roof - static_cast<double>(p.floodedDepth) - 0.5) +
                                 0.5;
            checks.flooded = checks.flooded && cell.wet && std::abs(cell.level - level) < 1.0e-3;
        }
    }
    return checks;
}

// Each type as the profile of every district, over flat ground of a mild climate (neutral wet
// chance 0.35, nothing frozen): the share of wet cells follows the chance of the profile (wet
// offset, then flooded), the cells wet without districts stay wet in the wetter profiles (the
// same draws), the levels rise by the level rise, the flooded cells stand just under the roof,
// and the cells freeze in the icy profile only.
void testCells() {
    const CaveGround ground = climateGround(100.0f, 12.0f, 0.5f);
    constexpr int kHalf = 24;
    const CaveField neutralField(kSeed, uniformCaves(CaveProfile{}));
    const AquiferParams& p = neutralField.params().aquifers;
    const double roof = static_cast<double>(neutralField.params().minRoof);
    const std::vector<AquiferCell> neutral = cellsOf(neutralField, ground, kHalf);
    for (int t = 0; t < kCaveDistrictTypeCount; ++t) {
        const CaveProfile profile = defaultProfile(typeAt(t));
        const CaveField field(kSeed, uniformCaves(profile));
        const std::vector<AquiferCell> cells = cellsOf(field, ground, kHalf);
        const CellChecks checks = checkCells(p, ground, roof, profile, neutral, cells);
        const double share = static_cast<double>(checks.wet) / static_cast<double>(cells.size());
        const double expected = expectedChance(p, profile, 0.5f);
        std::printf("  %-9s %4d of %zu cells wet (%.3f, chance %.3f), frozen %s\n",
                    terrain::caveDistrictName(typeAt(t)), checks.wet, cells.size(), share, expected,
                    checks.frozen ? "as the profile" : "WRONG");
        TEST_CHECK(std::abs(share - expected) < 0.04);
        TEST_CHECK(profile[CaveProfile::kWetOffset] >= 0.0f ? checks.supersets : checks.subsets);
        TEST_CHECK(checks.levelsShift && checks.frozen && checks.flooded);
    }
    TEST_CHECK(expectedChance(p, defaultProfile(CaveDistrictType::Volcanic), 0.5f) == 0.0);
    TEST_CHECK(expectedChance(p, defaultProfile(CaveDistrictType::Flooded), 0.5f) == 1.0);
}

// ---------------------------------------------------------------------------

// Samples of water in the windows, and the cave air they hold.
struct Water {
    std::size_t water = 0;
    std::size_t air = 0;        // carved air below the ground
    std::size_t underWater = 0; // of it, under the level of its column
};

Water measureWater(const CaveField& field, const CaveGround& ground,
                   const std::vector<Window>& windows, int depthStep) {
    Water result;
    for (const Window& w : windows) {
        forEachSample(field, ground, CaveDetail{}, w, depthStep,
                      [&](CaveSampler& sampler, const CaveColumn& column, double y, float rock) {
                          const float carved = sampler.density(column, y, rock);
                          result.water +=
                              sampler.waterDensity(column, y, rock, carved) < 0.0f ? 1 : 0;
                          const bool air = caves_test::carvedAir(rock, carved);
                          result.air += air ? 1 : 0;
                          result.underWater += air && y < column.waterLevel ? 1 : 0;
                      });
    }
    return result;
}

// Windows of 64 m around the sites of the first wet cells of the neutral caves over flat ground.
std::vector<Window> wetWindows(const CaveField& neutral, const CaveGround& ground) {
    const int roof = static_cast<int>(neutral.params().minRoof);
    const int deepest = static_cast<int>(neutral.params().maxDepth);
    AquiferCache cache(neutral, ground);
    std::vector<Window> windows;
    for (int cellZ = -4; cellZ < 4 && windows.size() < 3; ++cellZ) {
        for (int cellX = -4; cellX < 4 && windows.size() < 3; ++cellX) {
            const AquiferCell& cell = cache.at(cellX, cellZ);
            if (cell.wet) {
                windows.push_back(
                    {cell.siteX - 32.0, cell.siteZ - 32.0, 64.0, 64.0, 4.0, roof, deepest});
            }
        }
    }
    return windows;
}

// Over the same wet cells: the lakes districts hold at least half as much water again (their
// levels rise by 14 m), the flooded ones fill their caves, the volcanic ones are dry.
void testWaterVolume() {
    const CaveGround ground = climateGround(kFlatHeight, 12.0f, 0.5f);
    const CaveField neutralField(kSeed, uniformCaves(CaveProfile{}));
    const std::vector<Window> windows = wetWindows(neutralField, ground);
    TEST_CHECK(windows.size() == 3);
    const auto water = [&](CaveDistrictType type) {
        return measureWater(CaveField(kSeed, uniformCaves(defaultProfile(type))), ground, windows,
                            2);
    };
    const Water base = measureWater(neutralField, ground, windows, 2);
    const Water lakes = water(CaveDistrictType::Lakes);
    const Water flooded = water(CaveDistrictType::Flooded);
    const Water volcanic = water(CaveDistrictType::Volcanic);
    std::printf("water volume over %zu wet cells: %zu samples neutral, %zu lakes, %zu flooded, "
                "%zu volcanic\n",
                windows.size(), base.water, lakes.water, flooded.water, volcanic.water);
    TEST_CHECK(base.water >= 500);
    TEST_CHECK(2 * lakes.water >= 3 * base.water);
    TEST_CHECK(flooded.water > base.water && flooded.underWater * 10 >= flooded.air * 9);
    TEST_CHECK(volcanic.water == 0);
}

// ---------------------------------------------------------------------------

// Largest radius, a multiple of 100 m up to 1 km, of a square around the site of a district all
// of whose points (every 50 m, closer than the 280 m of the blend) belong to it alone.
int coreRadius(const CaveField& field, const CaveGround& ground, const terrain::CaveDistrict& d) {
    AquiferCache cache(field, ground);
    int radius = 0;
    for (int r = 100; r <= 1000; r += 100) {
        for (double z = d.siteZ - r; z <= d.siteZ + r; z += 50.0) {
            for (double x = d.siteX - r; x <= d.siteX + r; x += 50.0) {
                const terrain::DistrictWeights weights = cache.districtWeights(x, z);
                if (weights.count != 1 || weights.districts[0].type != d.type) {
                    return radius;
                }
            }
        }
        radius = r;
    }
    return radius;
}

// Window of 128 m around the site, from 1 m under the ground to the bottom of the caves.
Window siteWindow(const CaveField& field, const terrain::CaveDistrict& d) {
    const int bottom = static_cast<int>(field.params().maxDepth) + 2;
    return Window{d.siteX - 64.0, d.siteZ - 64.0, 128.0, 128.0, 4.0, 1, bottom};
}

struct FloodedCells {
    std::size_t cells = 0;   // aquifer cells whose site lies in the square of the core
    std::size_t flooded = 0; // of them wet, at the level just under the roof of their ground
};

FloodedCells floodedCellsIn(const CaveField& field, const CaveGround& ground,
                            const terrain::CaveDistrict& d, int radius) {
    const AquiferParams& p = field.params().aquifers;
    const double roof = static_cast<double>(field.params().minRoof);
    AquiferCache cache(field, ground);
    FloodedCells result;
    const auto first = [&p](double meters) {
        return static_cast<int>(std::floor(meters / p.cellSize));
    };
    for (int cellZ = first(d.siteZ - radius); cellZ <= first(d.siteZ + radius); ++cellZ) {
        for (int cellX = first(d.siteX - radius); cellX <= first(d.siteX + radius); ++cellX) {
            const AquiferCell& cell = cache.at(cellX, cellZ);
            if (std::abs(cell.siteX - d.siteX) > radius ||
                std::abs(cell.siteZ - d.siteZ) > radius) {
                continue;
            }
            const double level = std::floor(static_cast<double>(lowestGround(ground, p, cell)) -
                                            roof - static_cast<double>(p.floodedDepth) - 0.5) +
                                 0.5;
            ++result.cells;
            result.flooded += cell.wet && std::abs(cell.level - level) < 1.0e-3 ? 1 : 0;
        }
    }
    return result;
}

// Flooded districts of the default world, on land: in the core of each, every aquifer cell is
// wet up to just under the roof, at least 90% of the carved air lies under the level of its
// column, and no entrance opens in the square of the core, where the same square of neutral
// districts holds many.
void testFloodedCores() {
    const TerrainTiles tiles(kSeed, {}, {}, defaultCaves());
    const CaveField& field = *tiles.caves();
    const CaveGround& ground = tiles.caveGround();
    const CaveField neutral(kSeed, uniformCaves(CaveProfile{}));
    const std::vector<terrain::CaveDistrict> found =
        districtsOf(field, ground, {-8, 17}, CaveDistrictType::Flooded, 5.0f);
    TEST_CHECK(found.size() >= 3);
    for (std::size_t n = 0; n < std::min<std::size_t>(found.size(), 3); ++n) {
        const terrain::CaveDistrict& d = found[n];
        const int radius = coreRadius(field, ground, d);
        const double r = radius;
        const Water water = measureWater(field, ground, {siteWindow(field, d)}, 1);
        const FloodedCells cells = floodedCellsIn(field, ground, d, radius);
        const std::size_t entrances =
            field.entrancesNear(d.siteX - r, d.siteZ - r, d.siteX + r, d.siteZ + r, ground).size();
        const std::size_t others =
            neutral.entrancesNear(d.siteX - r, d.siteZ - r, d.siteX + r, d.siteZ + r, ground)
                .size();
        std::printf("flooded core at %.0f, %.0f (radius %d m): %zu of %zu cells flooded, %zu of "
                    "%zu air samples under water, %zu entrances (%zu without the districts)\n",
                    d.siteX, d.siteZ, radius, cells.flooded, cells.cells, water.underWater,
                    water.air, entrances, others);
        TEST_CHECK(radius >= 500);
        TEST_CHECK(cells.cells >= 4 && cells.flooded == cells.cells);
        TEST_CHECK(water.air >= 1000 && water.underWater * 10 >= water.air * 9);
        TEST_CHECK(entrances == 0 && others >= 5);
    }
}

} // namespace

int main() {
    testCells();
    testWaterVolume();
    testFloodedCores();
    return test::result();
}
