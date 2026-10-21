// Cliffs, overhangs and arches of the default world (the planetary geography), at a site found
// by search: highlands in a cliff province. Cliffs are found there; they only cut the eroded
// relief, by at most a terrace drop outside the gorges and cirques; the overhangs and arches
// leave the other columns exactly the cliffs; no arch is within reach of water.
#include "terrain/chunk_coord.hpp"
#include "terrain/generation/cliffs.hpp"
#include "terrain/generation/sculpted_region.hpp"

#include "default_world_support.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <optional>

namespace {

using default_world::Place;
using terrain::CliffRegion;
using terrain::HeightGrid;
using terrain::SculptedRegion;

// The center of a sample of the eroded grid.
Place cellCenter(const SculptedRegion& region, int index) {
    const terrain::ErodedRegion& eroded = region.cliffs().eroded();
    const int size = eroded.grid().size;
    return {eroded.sampleX(index % size), eroded.sampleZ(index / size)};
}

// Cells cut by the cliffs (surface over a meter under the eroded relief), and the bound: outside
// gorges and cirques, the surface lies between the eroded relief minus a terrace drop and it.
int testCuts(const CliffRegion& cliffs) {
    const HeightGrid& grid = cliffs.eroded().grid();
    const float cell = static_cast<float>(grid.metersPerPixel);
    const float drop = cliffs.field().maxTerraceDrop();
    const float cirqueReach = cliffs.field().params().cirqueRadius + cell;
    int cut = 0;
    bool bounded = true;
    for (int row = 0; row < grid.size; ++row) {
        for (int column = 0; column < grid.size; ++column) {
            const std::size_t i = grid.index(column, row);
            const float surface = cliffs.surface().heights[i];
            cut += surface < grid.heights[i] - 1.0f ? 1 : 0;
            const float halfWidth = cliffs.gorgeHalfWidth()[i];
            bool excluded = halfWidth > 0.0f && cliffs.riverDistance()[i] <= halfWidth + cell;
            for (const terrain::Cirque& cirque : cliffs.cirques()) {
                excluded = excluded || std::hypot(cliffs.eroded().sampleX(column) - cirque.x,
                                                  cliffs.eroded().sampleZ(row) - cirque.z) <=
                                           static_cast<double>(cirqueReach);
            }
            bounded = bounded && (excluded || (surface >= grid.heights[i] - drop - 2.0e-3f &&
                                               surface <= grid.heights[i] + 2.0e-3f));
        }
    }
    TEST_CHECK(bounded);
    return cut;
}

// Columns without overhang and arch are exactly those of the cliffs. Returns the columns with
// air below rock.
int testSculpted(const SculptedRegion& region) {
    const std::vector<float>& cliffSurface = region.cliffs().surface().heights;
    bool exact = true;
    int overhangs = 0;
    for (std::size_t i = 0; i < cliffSurface.size(); ++i) {
        const Place p = cellCenter(region, static_cast<int>(i));
        const terrain::SculptedColumn column = region.columnAt(p.x, p.z);
        overhangs += region.airBelowRock()[i] != 0 ? 1 : 0;
        if (column.overhang == 0.0f && column.arch == terrain::kNoArch &&
            region.overhangWeight()[i] == 0.0f) {
            exact = exact && region.surface().heights[i] == cliffSurface[i];
        }
    }
    TEST_CHECK(exact);
    return overhangs;
}

// No arch is within reach of a river or ocean cell.
void testArchesAwayFromWater(const SculptedRegion& region) {
    const terrain::ErodedRegion& eroded = region.cliffs().eroded();
    bool dry = true;
    for (const terrain::Arch& arch : region.arches()) {
        const double reach = region.archField().reach(arch);
        for (int i = 0; i < static_cast<int>(eroded.grid().heights.size()); ++i) {
            const Place p = cellCenter(region, i);
            const bool water =
                eroded.rivers().isRiver(i) ||
                eroded.grid().heights[static_cast<std::size_t>(i)] < terrain::kSeaLevel;
            dry = dry && !(water && std::hypot(p.x - arch.x, p.z - arch.z) <= reach);
        }
    }
    TEST_CHECK(dry);
}

} // namespace

int main() {
    const terrain::CliffField field(default_world::kSeed);
    const std::optional<Place> site = default_world::highlands(
        600.0f, 2000.0f, [&field](double x, double z) { return field.provinceAt(x, z) >= 0.9f; });
    TEST_CHECK(site.has_value());
    if (!site) {
        return test::result();
    }
    const double chunk = terrain::kChunkSizeMeters;
    const terrain::HeightmapRegion area{std::round(site->x / chunk) * chunk,
                                        std::round(site->z / chunk) * chunk, 50.0, 129};
    const SculptedRegion region(default_world::kSeed, area);
    const int cut = testCuts(region.cliffs());
    const int overhangs = testSculpted(region);
    testArchesAwayFromWater(region);
    std::printf("default world: cliff site at (%.1f, %.1f) km, %d cells cut, %d with air below "
                "rock, %zu arches, %zu cirques\n",
                area.centerX / 1000.0, area.centerZ / 1000.0, cut, overhangs,
                region.arches().size(), region.cliffs().cirques().size());
    TEST_CHECK(cut > 100);
    TEST_CHECK(overhangs > 0 || !region.arches().empty());
    return test::result();
}
