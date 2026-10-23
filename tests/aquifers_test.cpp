// Aquifers of the caves: deterministic cells (sites inside their jittered cell, levels at a half
// meter), the wet share following the humidity and the sea, the freezing, the levels below the
// ground, and the lakes read from a sampler over lattices of 1 m: water only in the cave air
// below the surface, never as a wall against the air of a neighbor (the barriers close the caves
// along the bisectors), entrances that stay dry, and the aquifer bit of the detail. Every
// parameter is read from the field.
#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/cave_entrances.hpp"
#include "terrain/generation/cave_sampler.hpp"

#include "aquifers_test_support.hpp"
#include "cave_environment_test_support.hpp"
#include "caves_test_support.hpp"
#include "legacy_relief_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

namespace {

using aquifers_test::BarrierCounts;
using aquifers_test::Bisector;
using aquifers_test::CellRange;
using aquifers_test::CellShare;
using aquifers_test::clearanceOver;
using aquifers_test::climateGround;
using aquifers_test::countBarriers;
using aquifers_test::countLakes;
using aquifers_test::EntranceWater;
using aquifers_test::findBisectors;
using aquifers_test::LakeCounts;
using aquifers_test::largestRadius;
using aquifers_test::latticeAround;
using aquifers_test::Scan;
using aquifers_test::shareOf;
using aquifers_test::waterInEntrance;
using aquifers_test::waterOver;
using aquifers_test::withoutAquifers;
using caves_test::dryEnvironment;
using caves_test::flatGround;
using caves_test::heightfieldGround;
using caves_test::humidEnvironment;
using caves_test::kSeed;
using caves_test::Lattice;
using caves_test::Rng;
using caves_test::withEnvironment;
using terrain::AquiferCache;
using terrain::AquiferCell;
using terrain::AquiferParams;
using terrain::CaveColumn;
using terrain::CaveDetail;
using terrain::CaveEntrance;
using terrain::CaveField;
using terrain::CaveGround;
using terrain::CaveParams;
using terrain::CaveSampler;
using terrain::Heightfield;
using terrain::kDryLevel;

constexpr float kFlatHeight = 300.0f;

// ---------------------------------------------------------------------------

// Same seed, same cells; another seed, other cells. Sites lie in their jittered cell, wet cells
// have a level at a half meter (never an integer) and dry cells the dry level.
void testCellsAreDeterministic() {
    const Heightfield heightfield(kSeed, test_relief::legacyRelief());
    const CaveGround ground = heightfieldGround(heightfield);
    const CaveField a(kSeed);
    const CaveField b(kSeed);
    const CaveField c(kSeed + 1);
    const AquiferParams& p = a.params().aquifers;
    constexpr int kHalf = 8;
    int cells = 0;
    int wet = 0;
    int differing = 0;
    bool same = true;
    bool inCell = true;
    bool halfMeter = true;
    bool dry = true;
    for (int cellZ = -kHalf; cellZ < kHalf; ++cellZ) {
        for (int cellX = -kHalf; cellX < kHalf; ++cellX) {
            const AquiferCell cell = a.aquifer(cellX, cellZ, ground);
            ++cells;
            same = same && cell == b.aquifer(cellX, cellZ, ground);
            differing += cell != c.aquifer(cellX, cellZ, ground) ? 1 : 0;
            inCell = inCell && cell.cellX == cellX && cell.cellZ == cellZ &&
                     std::abs(cell.siteX / p.cellSize - cellX - 0.5) <= p.jitter / 2.0 + 1.0e-9 &&
                     std::abs(cell.siteZ / p.cellSize - cellZ - 0.5) <= p.jitter / 2.0 + 1.0e-9;
            if (cell.wet) {
                ++wet;
                halfMeter = halfMeter && cell.level != std::floor(cell.level) &&
                            cell.level == std::floor(cell.level) + 0.5f;
            } else {
                dry = dry && cell.level == kDryLevel && !cell.frozen;
            }
        }
    }
    std::printf("cells: %d cells over the relief, %d wet, %d differ with seed + 1\n", cells, wet,
                differing);
    TEST_CHECK(same);
    TEST_CHECK(inCell);
    TEST_CHECK(halfMeter);
    TEST_CHECK(dry);
    TEST_CHECK(wet > 0 && wet < cells);
    TEST_CHECK(differing * 10 >= cells * 9);
}

// The cache gives the cells of the field, nearest finds the cell of the nearest site (as the
// sampler does), and highestLevelNear holds the level of every cell within reach.
void testCache() {
    const CaveField field(kSeed);
    const CaveGround humid = withEnvironment(flatGround(kFlatHeight), humidEnvironment());
    const CaveGround dry = withEnvironment(flatGround(kFlatHeight), dryEnvironment());
    const double size = field.params().aquifers.cellSize;
    AquiferCache cache(field, humid);
    bool atMatches = true;
    for (int cellZ = -5; cellZ <= 5; ++cellZ) {
        for (int cellX = -5; cellX <= 5; ++cellX) {
            const AquiferCell expected = field.aquifer(cellX, cellZ, humid);
            atMatches = atMatches && cache.at(cellX, cellZ) == expected &&
                        cache.at(cellX, cellZ) == expected;
        }
    }
    CaveSampler sampler(field, humid, CaveDetail{}, {-200.0, 200.0, -200.0, 200.0, 300.0, 200.0});
    Rng rng(21);
    bool nearestMatches = true;
    bool samplerMatches = true;
    for (int n = 0; n < 300; ++n) {
        const double x = rng.range(-1000.0, 1000.0);
        const double z = rng.range(-1000.0, 1000.0);
        const auto baseX = static_cast<int>(std::floor(x / size));
        const auto baseZ = static_cast<int>(std::floor(z / size));
        AquiferCell best = field.aquifer(baseX, baseZ, humid);
        for (int cellZ = baseZ - 2; cellZ <= baseZ + 2; ++cellZ) {
            for (int cellX = baseX - 2; cellX <= baseX + 2; ++cellX) {
                const AquiferCell cell = field.aquifer(cellX, cellZ, humid);
                const auto distance = [&](const AquiferCell& c) {
                    return std::hypot(x - c.siteX, z - c.siteZ);
                };
                best = distance(cell) < distance(best) ? cell : best;
            }
        }
        nearestMatches = nearestMatches && cache.nearest(x, z) == best;
        samplerMatches = samplerMatches && sampler.aquiferAt(x, z) == best;
    }
    constexpr double kReach = 20.0;
    AquiferCache dryCache(field, dry);
    int dryPoints = 0;
    bool covers = true;
    bool monotone = true;
    for (int n = 0; n < 400; ++n) {
        const double x = rng.range(-1500.0, 1500.0);
        const double z = rng.range(-1500.0, 1500.0);
        for (AquiferCache* c : {&cache, &dryCache}) {
            const float highest = c->highestLevelNear(x, z, kReach);
            const AquiferCell& home = c->nearest(x, z);
            covers = covers && highest >= (home.wet ? home.level : kDryLevel);
            monotone = monotone && c->highestLevelNear(x, z, 0.0) <= highest &&
                       highest <= c->highestLevelNear(x, z, 2.0 * kReach);
            for (int q = 0; q < 6; ++q) {
                const double dx = rng.range(-kReach, kReach);
                const double dz = rng.range(-kReach, kReach);
                const AquiferCell& other = c->nearest(x + dx, z + dz);
                covers =
                    covers && (std::hypot(dx, dz) > kReach || !other.wet || other.level <= highest);
            }
        }
        dryPoints += dryCache.highestLevelNear(x, z, kReach) == kDryLevel ? 1 : 0;
    }
    std::printf("cache: %d of 400 points of the dry ground have no water within %.0f m\n",
                dryPoints, kReach);
    TEST_CHECK(atMatches && nearestMatches && samplerMatches);
    TEST_CHECK(covers && monotone);
    TEST_CHECK(dryPoints > 100);
}

// The share of wet land cells grows with the humidity from dryChance to wetChance, and the
// cells under the sea are all wet.
void testWetShare() {
    const CaveField field(kSeed);
    const AquiferParams& p = field.params().aquifers;
    constexpr int kHalf = 60;
    const float middle = (p.dryHumidity + p.wetHumidity) / 2.0f;
    const CellShare dry = shareOf(field, climateGround(100.0f, 12.0f, p.dryHumidity / 4.0f), kHalf);
    const CellShare mid = shareOf(field, climateGround(100.0f, 12.0f, middle), kHalf);
    const CellShare humid = shareOf(field, climateGround(100.0f, 12.0f, 1.0f), kHalf);
    const CellShare shore = shareOf(field, climateGround(0.0f, 12.0f, p.dryHumidity / 4.0f), kHalf);
    const CellShare sea = shareOf(field, climateGround(-50.0f, 12.0f, p.dryHumidity / 4.0f), kHalf);
    const CellShare shallow =
        shareOf(field, climateGround(-0.5f, 12.0f, p.dryHumidity / 4.0f), kHalf);
    const auto fraction = [](const CellShare& s) { return static_cast<double>(s.wet) / s.cells; };
    std::printf("wet share: dry %.3f (%.2f), middle %.3f (%.2f), humid %.3f (%.2f), sea %.3f\n",
                fraction(dry), static_cast<double>(p.dryChance), fraction(mid),
                static_cast<double>((p.dryChance + p.wetChance) / 2.0f), fraction(humid),
                static_cast<double>(p.wetChance), fraction(sea));
    TEST_CHECK(std::abs(fraction(dry) - static_cast<double>(p.dryChance)) < 0.02);
    TEST_CHECK(std::abs(fraction(mid) - static_cast<double>((p.dryChance + p.wetChance) / 2.0f)) <
               0.02);
    TEST_CHECK(std::abs(fraction(humid) - static_cast<double>(p.wetChance)) < 0.02);
    TEST_CHECK(fraction(dry) < fraction(mid) && fraction(mid) < fraction(humid));
    // Sea level itself is land; under it every cell is wet, whatever the humidity.
    TEST_CHECK(std::abs(fraction(shore) - static_cast<double>(p.dryChance)) < 0.02);
    TEST_CHECK(sea.wet == sea.cells && shallow.wet == shallow.cells);
}

// A wet cell is frozen exactly when the temperature of its surface is at most the freezing
// temperature; a dry cell never is.
void testFreezing() {
    const CaveField field(kSeed);
    const AquiferParams& p = field.params().aquifers;
    constexpr int kHalf = 40;
    const auto share = [&](float temperature) {
        return shareOf(field, climateGround(100.0f, temperature, 1.0f), kHalf);
    };
    const CellShare cold = share(p.freezeTemperature - 10.0f);
    const CellShare onTheEdge = share(p.freezeTemperature);
    const CellShare justAbove = share(std::nextafter(p.freezeTemperature, 100.0f));
    const CellShare warm = share(p.freezeTemperature + 10.0f);
    std::printf("freezing: %d wet cells of %d, frozen %d (cold), %d (at %.1f C), %d (just above), "
                "%d (warm)\n",
                cold.wet, cold.cells, cold.frozen, onTheEdge.frozen,
                static_cast<double>(p.freezeTemperature), justAbove.frozen, warm.frozen);
    TEST_CHECK(cold.wet > 100);
    TEST_CHECK(cold.frozen == cold.wet && onTheEdge.frozen == onTheEdge.wet);
    TEST_CHECK(justAbove.frozen == 0 && warm.frozen == 0);
    TEST_CHECK(cold.wet == warm.wet);
}

// Lowest ground of a cell (of the canonical range): its site and the 3 x 3 probes of its square
// of the grid of the world (cells of about cellSize).
float lowestGround(const CaveField& field, const CaveGround& ground, const AquiferCell& cell) {
    const double sizeX = field.aquiferCellsX().size;
    const double sizeZ = field.aquiferCellsZ().size;
    float lowest = ground.height(cell.siteX, cell.siteZ);
    for (int probe = 0; probe < 9; ++probe) {
        lowest = std::min(lowest, ground.height((cell.cellX + 0.5 * (probe % 3)) * sizeX,
                                                (cell.cellZ + 0.5 * (probe / 3)) * sizeZ));
    }
    return lowest;
}

// Levels under the lowest ground of their cell (the site and the 3 x 3 probes of its grid
// square) by the roof and a depth drawn in [minLevelDepth, maxLevelDepth]. The level is then
// rounded down to n + 0.5, so it can stand up to 1 m deeper than that, never shallower.
void checkLevels(const CaveField& field, const CaveGround& ground, int half, const char* name) {
    const AquiferParams& p = field.params().aquifers;
    const double roof = field.params().minRoof;
    int wet = 0;
    bool inRange = true;
    double closest = 1.0e9;
    double deepest = 0.0;
    for (int cellZ = -half; cellZ < half; ++cellZ) {
        for (int cellX = -half; cellX < half; ++cellX) {
            const AquiferCell cell = field.aquifer(cellX, cellZ, ground);
            if (!cell.wet) {
                continue;
            }
            ++wet;
            const double below = static_cast<double>(lowestGround(field, ground, cell)) -
                                 static_cast<double>(cell.level);
            closest = std::min(closest, below);
            deepest = std::max(deepest, below);
            inRange = inRange && below >= roof + p.minLevelDepth - 1.0e-3 &&
                      below <= roof + p.maxLevelDepth + 1.0 + 1.0e-3;
        }
    }
    std::printf("levels (%s): %d wet cells, %.2f to %.2f m under the lowest ground (roof %.0f, "
                "depth %.0f to %.0f)\n",
                name, wet, closest, deepest, roof, static_cast<double>(p.minLevelDepth),
                static_cast<double>(p.maxLevelDepth));
    TEST_CHECK(wet >= 20);
    TEST_CHECK(inRange);
    TEST_CHECK(closest > roof);
}

void testLevelsBelowGround() {
    const CaveField field(kSeed);
    const Heightfield heightfield(kSeed, test_relief::legacyRelief());
    checkLevels(field, withEnvironment(flatGround(kFlatHeight + 0.3f), humidEnvironment()), 40,
                "flat");
    checkLevels(field, withEnvironment(heightfieldGround(heightfield), humidEnvironment()), 8,
                "relief");
}

// ---------------------------------------------------------------------------

// The lakes of the lattices around some bisectors: every sample of water lies in the cave air
// (within the skin of the rock), under the level of its column and under the ground; the cave
// air under them is water (the lakes are full); dry columns hold none; and no water stands as
// a wall against the air: the barriers close the caves along the bisectors, which the same
// caves without barriers would not.
void checkLakes(const CaveField& field, const CaveGround& ground, const CellRange& range,
                float minHeight, const char* name) {
    const std::vector<Bisector> sites = findBisectors(field, ground, range, minHeight);
    constexpr std::size_t kLattices = 8;
    const std::size_t stride = std::max<std::size_t>(1, sites.size() / kLattices);
    const CaveDetail bare = withoutAquifers();
    LakeCounts lakes;
    LakeCounts unclosed;
    BarrierCounts barriers;
    std::size_t scanned = 0;
    bool sameWater = true;
    for (std::size_t n = 0; n < sites.size() && scanned < kLattices; n += stride, ++scanned) {
        const Lattice lattice = latticeAround(sites[n], 16, 24, 8);
        const Scan with(field, ground, CaveDetail{}, lattice);
        const Scan without(field, ground, bare, lattice);
        sameWater = sameWater && waterOver(field, with, with.carved()) == with.water();
        lakes.add(countLakes(field, with, with.carved(), with.water()));
        unclosed.add(
            countLakes(field, with, without.carved(), waterOver(field, with, without.carved())));
        barriers.add(countBarriers(field, with, without));
    }
    std::printf("lakes (%s): %zu bisectors, %zu lattices, %zu water samples (%zu in the skin), "
                "%zu barrier samples close the caves, %zu water walls without barriers\n",
                name, sites.size(), scanned, lakes.water, lakes.inRock, barriers.closed,
                unclosed.walls);
    TEST_CHECK(scanned == kLattices);
    TEST_CHECK(sameWater);
    TEST_CHECK(lakes.water >= 500);
    TEST_CHECK(lakes.outsideAir == 0 && lakes.aboveLevel == 0 && lakes.aboveGround == 0);
    TEST_CHECK(lakes.dryAir == 0 && lakes.dryColumns == 0);
    TEST_CHECK(lakes.walls == 0);
    TEST_CHECK(barriers.closed >= 100);
    TEST_CHECK(barriers.strayed == 0 && barriers.open == 0 && barriers.changed == 0);
    TEST_CHECK(barriers.added == 0);
    TEST_CHECK(unclosed.walls > 0);
}

void testLakes() {
    const CaveField field(kSeed);
    const Heightfield heightfield(kSeed, test_relief::legacyRelief());
    checkLakes(field, withEnvironment(flatGround(kFlatHeight), humidEnvironment()), {-6, -6, 12},
               0.0f, "flat");
    checkLakes(field, withEnvironment(heightfieldGround(heightfield), humidEnvironment()),
               {1, -14, 8}, 20.0f, "relief");
}

// ---------------------------------------------------------------------------

// Over wet ground, the entrances that would reach the water or the barriers around them are
// dropped, and the others stay dry: no sample of their tube or chamber holds water.
void testEntrancesStayDry() {
    const CaveGround ground = withEnvironment(flatGround(kFlatHeight), humidEnvironment());
    CaveParams noWater;
    noWater.aquifers.dryChance = 0.0f;
    noWater.aquifers.wetChance = 0.0f;
    const CaveField field(kSeed);
    const CaveField bare(kSeed, noWater);
    constexpr double kHalf = 2048.0;
    const std::vector<CaveEntrance> kept =
        field.entrancesNear(-kHalf, -kHalf, kHalf, kHalf, ground);
    const std::vector<CaveEntrance> all = bare.entrancesNear(-kHalf, -kHalf, kHalf, kHalf, ground);
    const auto listed = [](const std::vector<CaveEntrance>& list, const CaveEntrance& e) {
        return std::find(list.begin(), list.end(), e) != list.end();
    };
    AquiferCache cache(field, ground);
    const double margin = static_cast<double>(field.params().aquifers.entranceMargin);
    bool subset = true;
    bool keptClear = true;
    int dropped = 0;
    int reachedWater = 0;
    for (const CaveEntrance& e : kept) {
        subset = subset && listed(all, e);
        keptClear = keptClear && clearanceOver(e, cache) >= margin;
    }
    for (const CaveEntrance& e : all) {
        if (!listed(kept, e)) {
            ++dropped;
            reachedWater += clearanceOver(e, cache) < margin ? 1 : 0;
        }
    }
    std::size_t inside = 0;
    std::size_t wet = 0;
    int checked = 0;
    for (const CaveEntrance& e : kept) {
        const bool overWater =
            cache.highestLevelNear(e.bottomX, e.bottomZ, largestRadius(e)) > kDryLevel ||
            cache.highestLevelNear(e.topX, e.topZ, largestRadius(e)) > kDryLevel;
        if (overWater && checked < 4) {
            const EntranceWater water = waterInEntrance(field, ground, e);
            inside += water.inside;
            wet += water.wet;
            ++checked;
        }
    }
    std::printf("entrances: %zu without water, %zu with, %d dropped (%d reach the water); %d over "
                "wet cells checked: %zu samples of tube and chamber, %zu with water\n",
                all.size(), kept.size(), dropped, reachedWater, checked, inside, wet);
    TEST_CHECK(subset && keptClear);
    TEST_CHECK(dropped >= 5 && reachedWater == dropped);
    TEST_CHECK(checked == 4);
    TEST_CHECK(inside >= 2000);
    TEST_CHECK(wet == 0);
}

// Without kCaveAquifers the detail carves no barrier and no water, even over wet cells; with
// it, the void of the barriers is the one of the columns.
void testAquiferBit() {
    const CaveField field(kSeed);
    const CaveGround ground = withEnvironment(flatGround(kFlatHeight), humidEnvironment());
    const std::vector<Bisector> sites = findBisectors(field, ground, {-6, -6, 12}, 0.0f);
    TEST_CHECK(!sites.empty());
    if (sites.empty()) {
        return;
    }
    // The first bisector whose lattice holds water (caves carved under the level).
    std::size_t chosen = 0;
    for (std::size_t n = 0; n < std::min<std::size_t>(sites.size(), 8); ++n) {
        const Scan probe(field, ground, CaveDetail{}, latticeAround(sites[n], 16, 24, 8));
        if (std::any_of(probe.water().begin(), probe.water().end(),
                        [](float w) { return w < 0.0f; })) {
            chosen = n;
            break;
        }
    }
    const Lattice l = latticeAround(sites[chosen], 16, 24, 8);
    const CaveDetail bare = withoutAquifers();
    const Scan with(field, ground, CaveDetail{}, l);
    const Scan without(field, ground, bare, l);
    const float clamp = field.params().voidClamp;
    const terrain::CaveBox box{static_cast<double>(l.x0),
                               static_cast<double>(l.y0),
                               static_cast<double>(l.z0),
                               static_cast<double>(l.x0 + l.nx - 1),
                               static_cast<double>(l.y0 + l.ny - 1),
                               static_cast<double>(l.z0 + l.nz - 1)};
    CaveSampler withSampler(field, ground, CaveDetail{}, box);
    CaveSampler bareSampler(field, ground, bare, box);
    bool noWater = std::all_of(without.water().begin(), without.water().end(),
                               [clamp](float w) { return w == clamp; });
    bool dryColumns = true;
    bool barrierVoid = true;
    int barriers = 0;
    int waterOverWet = 0;
    for (int k = 0; k < l.nz; ++k) {
        for (int j = 0; j < l.ny; ++j) {
            for (int i = 0; i < l.nx; ++i) {
                const CaveColumn& wet = with.column(i, k);
                const CaveColumn& dry = without.column(i, k);
                const double y = with.y(j);
                const std::size_t at = l.index(i, j, k);
                const float slab = std::max(-clamp, aquifers_test::slabAt(wet, y));
                dryColumns = dryColumns && dry.waterLevel == kDryLevel && !dry.frozen &&
                             dry.barrierCount == 0;
                barrierVoid = barrierVoid && bareSampler.voids(dry, y).barrier == -clamp &&
                              withSampler.voids(wet, y).barrier == slab;
                barriers += wet.barrierCount > 0 ? 1 : 0;
                noWater = noWater && bareSampler.waterDensity(wet, y, with.rock()[at],
                                                              with.carved()[at]) == clamp;
                waterOverWet += with.water()[at] < 0.0f ? 1 : 0;
            }
        }
    }
    std::printf("aquifer bit: %d samples in barrier columns, %d with water with the bit, none "
                "without\n",
                barriers, waterOverWet);
    TEST_CHECK(noWater && dryColumns && barrierVoid);
    TEST_CHECK(barriers > 0 && waterOverWet > 0);
}

} // namespace

int main() {
    testCellsAreDeterministic();
    testCache();
    testWetShare();
    testFreezing();
    testLevelsBelowGround();
    testLakes();
    testEntrancesStayDry();
    testAquiferBit();
    return test::result();
}
