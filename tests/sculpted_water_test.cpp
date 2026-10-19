// Rivers, cascades, cirques, oceans and coasts of a sculpted region: the overhangs and the
// arches never reach them, so they are exactly those of the cliff region (Phase 5A).
// Overhangs and arches themselves: sculpted_region_test.cpp. Every parameter is read from
// the fields, nothing is hard-coded.
#include "terrain/generation/sculpted_region.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::Arch;
using terrain::CliffRegion;
using terrain::HeightGrid;
using terrain::HeightmapRegion;
using terrain::SculptedColumn;
using terrain::SculptedRegion;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

struct Point {
    double x;
    double z;
};

Point cellCenter(const SculptedRegion& region, int index) {
    const terrain::ErodedRegion& eroded = region.cliffs().eroded();
    const int size = eroded.grid().size;
    return {eroded.sampleX(index % size), eroded.sampleZ(index / size)};
}

// True when the column at the cell is exactly a column of the cliffs: no overhang, no arch.
bool untouched(const SculptedRegion& region, int index) {
    const Point p = cellCenter(region, index);
    const SculptedColumn column = region.columnAt(p.x, p.z);
    const auto cell = static_cast<std::size_t>(index);
    return column.overhang == 0.0f && column.arch == terrain::kNoArch &&
           region.overhangWeight()[cell] == 0.0f && region.airBelowRock()[cell] == 0 &&
           region.surface().heights[cell] == region.cliffs().surface().heights[cell];
}

// True if a cell is a coast: land within two cells of an ocean cell.
bool isCoast(const HeightGrid& grid, int index) {
    const int column = index % grid.size;
    const int row = index / grid.size;
    if (grid.at(column, row) < terrain::kSeaLevel) {
        return false;
    }
    for (int r = std::max(0, row - 2); r <= std::min(grid.size - 1, row + 2); ++r) {
        for (int c = std::max(0, column - 2); c <= std::min(grid.size - 1, column + 2); ++c) {
            if (grid.at(c, r) < terrain::kSeaLevel) {
                return true;
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------

// The cliff region inside is exactly the cliff region built alone: Phase 5A is unchanged.
void testCliffsUnchanged(const SculptedRegion& region, const HeightmapRegion& site) {
    const CliffRegion plain(kSeed, site);
    const CliffRegion& cliffs = region.cliffs();
    TEST_CHECK(cliffs.surface().heights == plain.surface().heights);
    TEST_CHECK(cliffs.mask() == plain.mask());
    TEST_CHECK(cliffs.cirques() == plain.cirques());
    TEST_CHECK(cliffs.eroded().grid().heights == plain.eroded().grid().heights);
    TEST_CHECK(cliffs.eroded().rivers().river == plain.eroded().rivers().river);
    TEST_CHECK(cliffs.eroded().rivers().cascades == plain.eroded().rivers().cascades);
}

// Every river cell keeps its column: water exactly at the surface, air above, rock below.
void testRivers(const SculptedRegion& region) {
    const HeightGrid& grid = region.cliffs().eroded().grid();
    int rivers = 0;
    bool kept = true;
    bool water = true;
    for (int i = 0; i < static_cast<int>(grid.heights.size()); ++i) {
        if (!region.cliffs().eroded().rivers().isRiver(i)) {
            continue;
        }
        ++rivers;
        const Point p = cellCenter(region, i);
        const double level = grid.heights[static_cast<std::size_t>(i)];
        kept = kept && untouched(region, i);
        water = water && std::abs(region.surface().heights[static_cast<std::size_t>(i)] - level) <=
                             2.0e-3 &&
                region.densityAt(p.x, level + 0.5, p.z) > 0.0f &&
                region.densityAt(p.x, level - 0.5, p.z) < 0.0f;
    }
    std::printf("rivers: %d river cells\n", rivers);
    TEST_CHECK(rivers > 50);
    TEST_CHECK(kept);
    TEST_CHECK(water);
}

// Cascades stay on their water; cirques stay open above their floor, as in the cliffs.
void testCascadesAndCirques(const SculptedRegion& region) {
    const terrain::ErodedRegion& eroded = region.cliffs().eroded();
    bool cascades = true;
    for (const terrain::Cascade& cascade : eroded.rivers().cascades) {
        const int index = cascade.row * eroded.grid().size + cascade.column;
        const Point p = cellCenter(region, index);
        cascades = cascades && untouched(region, index) &&
                   std::abs(region.surfaceAt(p.x, p.z) - cascade.top) <= 2.0e-3f;
    }
    TEST_CHECK(cascades);
    const float radius = region.cliffs().field().params().cirqueRadius;
    bool cirques = true;
    for (const terrain::Cirque& cirque : region.cliffs().cirques()) {
        for (const double r : {0.0, 0.5 * radius, 0.9 * radius}) {
            for (const double angle : {0.0, 1.7, 3.4, 5.1}) {
                const double x = cirque.x + r * std::cos(angle);
                const double z = cirque.z + r * std::sin(angle);
                // Air above the floor, and rock and air below it as in the cliffs.
                for (double y = cirque.floor - 30.0; y <= cirque.floor + 30.0; y += 1.0) {
                    const bool rock = region.densityAt(x, y, z) < 0.0f;
                    cirques = cirques && rock == (region.cliffs().densityAt(x, y, z) < 0.0f) &&
                              !(rock && y > static_cast<double>(cirque.floor) + 0.5);
                }
            }
        }
    }
    TEST_CHECK(cirques);
}

// Oceans and coasts keep their columns, and no arch reaches them.
void testOceansAndCoasts(const SculptedRegion& region, bool expectOcean) {
    const HeightGrid& grid = region.cliffs().eroded().grid();
    int ocean = 0;
    int coast = 0;
    bool kept = true;
    for (int i = 0; i < static_cast<int>(grid.heights.size()); ++i) {
        const auto cell = static_cast<std::size_t>(i);
        if (grid.heights[cell] < terrain::kSeaLevel) {
            ++ocean;
            kept = kept && untouched(region, i) && region.surface().heights[cell] == grid.heights[cell];
        } else if (isCoast(grid, i)) {
            ++coast;
            kept = kept && untouched(region, i);
        }
    }
    std::printf("oceans: %d ocean cells, %d coast cells\n", ocean, coast);
    TEST_CHECK(kept);
    TEST_CHECK(!expectOcean || (ocean > 0 && coast > 0));
}

// No arch is within reach of water: no river or ocean cell, no cirque near its reach.
void testArchesAwayFromWater(const SculptedRegion& region) {
    const terrain::ArchField& field = region.archField();
    const terrain::CliffParams& cliffs = region.cliffs().field().params();
    const HeightGrid& grid = region.cliffs().eroded().grid();
    bool dry = true;
    for (const Arch& arch : region.arches()) {
        const double reach = field.reach(arch);
        for (int i = 0; i < static_cast<int>(grid.heights.size()); ++i) {
            const Point p = cellCenter(region, i);
            const bool water = region.cliffs().eroded().rivers().isRiver(i) ||
                               grid.heights[static_cast<std::size_t>(i)] < terrain::kSeaLevel;
            dry = dry && !(water && std::hypot(p.x - arch.x, p.z - arch.z) <= reach);
        }
        for (const terrain::Cirque& cirque : region.cliffs().cirques()) {
            dry = dry && std::hypot(cirque.x - arch.x, cirque.z - arch.z) >=
                             reach + cliffs.cirqueRadius + 2.0 * cliffs.densityClamp;
        }
    }
    TEST_CHECK(dry);
}

} // namespace

int main() {
    const HeightmapRegion site = terrain::findCliffRegion(kSeed, 50.0, 129);
    const SculptedRegion region(kSeed, site);
    testCliffsUnchanged(region, site);
    testRivers(region);
    testCascadesAndCirques(region);
    testOceansAndCoasts(region, false);
    testArchesAwayFromWater(region);

    // A coast with cliffs inland: the ocean, the coastline and the rivers reaching the sea are
    // untouched, and so are the arches placed there.
    const HeightmapRegion shore{.centerX = 10000.0, .centerZ = 10000.0, .metersPerPixel = 100.0,
                                .size = 128};
    const SculptedRegion coast(kSeed, shore);
    std::printf("coast: %zu arches\n", coast.arches().size());
    TEST_CHECK(!coast.arches().empty());
    testCliffsUnchanged(coast, shore);
    testRivers(coast);
    testCascadesAndCirques(coast);
    testOceansAndCoasts(coast, true);
    testArchesAwayFromWater(coast);
    return test::result();
}
