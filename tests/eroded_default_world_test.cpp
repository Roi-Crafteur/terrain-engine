// Eroded regions of the default world (the planetary geography), around a coast and mountains
// with cascades found by search: finite heights within the original range and no hole, the coast
// and the oceans untouched, rivers that never flow uphill down to the sea or the border, cascades
// only where a river steepens, and the relief outside the region exactly the Heightfield.
#include "terrain/generation/eroded_region.hpp"

#include "default_world_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <optional>

namespace {

using default_world::Place;
using terrain::ErodedRegion;
using terrain::HeightGrid;
using terrain::HeightmapRegion;
using terrain::kNoReceiver;
using terrain::kSeaLevel;
using terrain::RiverNetwork;

float heightOf(const HeightGrid& grid, int i) {
    return grid.heights[static_cast<std::size_t>(i)];
}

bool isBorder(const HeightGrid& grid, int i) {
    const int column = i % grid.size;
    const int row = i / grid.size;
    return column == 0 || row == 0 || column == grid.size - 1 || row == grid.size - 1;
}

// Finite heights within the original range; every inner land cell has a neighbor at or below it.
void testNoHoles(const ErodedRegion& eroded) {
    const HeightGrid& grid = eroded.grid();
    const auto [low, high] =
        std::minmax_element(eroded.rawGrid().heights.begin(), eroded.rawGrid().heights.end());
    bool valid = true;
    bool noHole = true;
    for (int i = 0; i < static_cast<int>(grid.heights.size()); ++i) {
        const float h = heightOf(grid, i);
        valid = valid && std::isfinite(h) && h >= *low && h <= *high;
        if (h < kSeaLevel || isBorder(grid, i)) {
            continue;
        }
        float lowest = std::numeric_limits<float>::max();
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx != 0 || dz != 0) {
                    lowest = std::min(lowest, heightOf(grid, i + dz * grid.size + dx));
                }
            }
        }
        noHole = noHole && lowest <= h;
    }
    TEST_CHECK(valid);
    TEST_CHECK(noHole);
}

// Oceans keep their heights and land stays land.
void testCoastKept(const ErodedRegion& eroded) {
    bool kept = true;
    for (std::size_t i = 0; i < eroded.rawGrid().heights.size(); ++i) {
        const float raw = eroded.rawGrid().heights[i];
        const float h = eroded.grid().heights[i];
        kept = kept && (raw < kSeaLevel ? h == raw : h >= kSeaLevel);
    }
    TEST_CHECK(kept);
}

// Each river cell flows to a river cell or into the ocean, never uphill, to the sea or the
// border. Returns the number of river cells.
int testRivers(const ErodedRegion& eroded) {
    const HeightGrid& grid = eroded.grid();
    const RiverNetwork& rivers = eroded.rivers();
    int count = 0;
    bool connected = true;
    for (int i = 0; i < static_cast<int>(grid.heights.size()); ++i) {
        if (!rivers.isRiver(i)) {
            continue;
        }
        ++count;
        int cell = i;
        int steps = 0;
        while (rivers.receiverOf(cell) != kNoReceiver && steps++ <= grid.size * grid.size) {
            const int next = rivers.receiverOf(cell);
            connected = connected && (rivers.isRiver(next) || heightOf(grid, next) < kSeaLevel) &&
                        heightOf(grid, next) <= heightOf(grid, cell);
            cell = next;
        }
        connected = connected && (heightOf(grid, cell) < kSeaLevel || isBorder(grid, cell));
    }
    TEST_CHECK(connected);
    return count;
}

// Cascades lie on rivers where the slope below is steep enough and steeper than above.
void testCascades(const ErodedRegion& eroded, bool expected) {
    const terrain::RiverParams params;
    const RiverNetwork& rivers = eroded.rivers();
    bool rule = true;
    for (const terrain::Cascade& c : rivers.cascades) {
        rule = rule && rivers.isRiver(c.row * eroded.grid().size + c.column) &&
               c.slopeBelow >= params.cascadeMinSlope &&
               c.slopeBelow - c.slopeAbove >= params.cascadeMinBreak && c.top >= c.bottom;
    }
    TEST_CHECK(rule);
    TEST_CHECK(!expected || !rivers.cascades.empty());
}

// Outside the region, a few hundred meters beyond each side, the relief is the Heightfield.
void testOutside(const ErodedRegion& eroded, const HeightmapRegion& region) {
    const double half = 0.5 * region.size * region.metersPerPixel + 300.0;
    bool same = true;
    for (const auto& [dx, dz] : std::array<std::array<double, 2>, 4>{
             {{half, 0.0}, {-half, 17.0}, {33.0, half}, {-5.0, -half}}}) {
        const double x = region.centerX + dx;
        const double z = region.centerZ + dz;
        same = same && eroded.heightAt(x, z) == default_world::field().heightAt(x, z);
    }
    TEST_CHECK(same);
}

// Mountains (the mountain layer above 1,500 m) whose eroded region of 10 km holds cascades: the
// first of a lattice of 32 km, out to 1,300 km. About half the mountain regions have some.
std::optional<Place> cascadeSite() {
    return default_world::nearest(32768.0, 40, [](double x, double z) {
        return default_world::field().sampleAt(x, z).mountain > 1500.0f &&
               !ErodedRegion(default_world::kSeed, {x, z, 80.0, 128}).rivers().cascades.empty();
    });
}

} // namespace

int main() {
    const std::optional<Place> mountain = cascadeSite();
    const std::optional<Place> coast = default_world::coast();
    TEST_CHECK(mountain && coast);
    if (!mountain || !coast) {
        return test::result();
    }
    const HeightmapRegion mountainRegion{mountain->x, mountain->z, 80.0, 128};
    const HeightmapRegion coastRegion{coast->x, coast->z, 100.0, 128};
    const ErodedRegion mountains(default_world::kSeed, mountainRegion);
    const ErodedRegion shore(default_world::kSeed, coastRegion);
    for (const ErodedRegion* eroded : {&mountains, &shore}) {
        testNoHoles(*eroded);
        testCoastKept(*eroded);
    }
    const int mountainRivers = testRivers(mountains);
    const int coastRivers = testRivers(shore);
    testCascades(mountains, true);
    testCascades(shore, false);
    testOutside(mountains, mountainRegion);
    testOutside(shore, coastRegion);
    std::printf("default world: mountain at (%.0f, %.0f) km, %d river cells, %zu cascades; coast "
                "at (%.0f, %.0f) km, %d river cells\n",
                mountain->x / 1000.0, mountain->z / 1000.0, mountainRivers,
                mountains.rivers().cascades.size(), coast->x / 1000.0, coast->z / 1000.0,
                coastRivers);
    TEST_CHECK(mountainRivers > 50);
    return test::result();
}
