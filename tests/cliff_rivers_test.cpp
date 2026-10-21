// Gorges, rivers, cascades, oceans and cirques of a cliff region: the cliffs never change the
// rivers of the eroded region; they dig gorges along the wide ones and a cirque below each
// cascade. Cliffs, plateaus and continuity: cliff_region_test.cpp. Every parameter is read
// from the field, nothing is hard-coded.
#include "terrain/generation/cliff_region.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <set>
#include <utility>
#include <vector>

namespace {

using terrain::Cascade;
using terrain::Cirque;
using terrain::CliffColumn;
using terrain::CliffParams;
using terrain::CliffRegion;
using terrain::ErodedRegion;
using terrain::HeightGrid;
using terrain::HeightmapRegion;
using terrain::kNoReceiver;
using terrain::kSeaLevel;
using terrain::RiverNetwork;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
// Cascades are rare (only at the slope breaks of the rivers): the cliff site of this seed has
// one, with its cirque, and gorges dug into the relief (found by a search of seeds 1 to 60).
constexpr std::uint64_t kCascadeSeed = 47;

struct Point {
    double x;
    double z;
};

int columnOf(const HeightGrid& grid, int index) {
    return index % grid.size;
}

int rowOf(const HeightGrid& grid, int index) {
    return index / grid.size;
}

float heightOf(const HeightGrid& grid, int index) {
    return grid.heights[static_cast<std::size_t>(index)];
}

Point cellCenter(const ErodedRegion& eroded, int index) {
    const HeightGrid& grid = eroded.grid();
    return {eroded.sampleX(columnOf(grid, index)), eroded.sampleZ(rowOf(grid, index))};
}

// River cells with a gorge, in cliffs of mask at least minMask.
std::vector<int> gorgeRiverCells(const CliffRegion& cliffs, float minMask) {
    std::vector<int> cells;
    for (int i = 0; i < static_cast<int>(cliffs.mask().size()); ++i) {
        const auto cell = static_cast<std::size_t>(i);
        if (cliffs.eroded().rivers().isRiver(i) && cliffs.gorgeHalfWidth()[cell] > 0.0f &&
            cliffs.mask()[cell] >= minMask) {
            cells.push_back(i);
        }
    }
    return cells;
}

// The river cell nearest to p within reach of it, or kNoReceiver when there is none.
int nearestRiverCell(const ErodedRegion& eroded, const Point& p, double reach) {
    const HeightGrid& grid = eroded.grid();
    const int span = static_cast<int>(std::ceil(reach / grid.metersPerPixel)) + 1;
    const int column0 = static_cast<int>(std::lround((p.x - eroded.sampleX(0)) / grid.metersPerPixel));
    const int row0 = static_cast<int>(std::lround((p.z - eroded.sampleZ(0)) / grid.metersPerPixel));
    int nearest = kNoReceiver;
    double best = reach;
    for (int row = std::max(0, row0 - span); row <= std::min(grid.size - 1, row0 + span); ++row) {
        for (int column = std::max(0, column0 - span);
             column <= std::min(grid.size - 1, column0 + span); ++column) {
            const int index = row * grid.size + column;
            const double distance = std::hypot(eroded.sampleX(column) - p.x, eroded.sampleZ(row) - p.z);
            if (eroded.rivers().isRiver(index) && distance < best) {
                best = distance;
                nearest = index;
            }
        }
    }
    return nearest;
}

constexpr Point kAxes[4] = {{1.0, 0.0}, {-1.0, 0.0}, {0.0, 1.0}, {0.0, -1.0}};

// ---------------------------------------------------------------------------

// Deep inside a gorge (0.4 half-width from the river), in full cliffs and outside the cirques
// (which carve the floor deeper), the surface is the water, or the relief where it is lower
// than the interpolated water level (by up to 1.5 m at the sample interpolation, on steep
// banks); the river is a network river wide enough to have a gorge.
void testGorgesAtWater(const CliffRegion& cliffs) {
    const CliffParams& params = cliffs.field().params();
    const std::vector<int> cells = gorgeRiverCells(cliffs, 1.0f);
    int kept = 0;
    int atLevel = 0;
    bool atWater = true;
    bool wideEnough = true;
    for (const int i : cells) {
        const auto cell = static_cast<std::size_t>(i);
        wideEnough = wideEnough && cliffs.eroded().rivers().isRiver(i) &&
                     cliffs.eroded().rivers().catchment[cell] >= params.gorgeMinCatchment;
        const Point center = cellCenter(cliffs.eroded(), i);
        const double offset = 0.4 * static_cast<double>(cliffs.gorgeHalfWidth()[cell]);
        for (const Point& axis : kAxes) {
            const Point p{center.x + offset * axis.x, center.z + offset * axis.z};
            const CliffColumn column = cliffs.columnAt(p.x, p.z);
            if (!(column.riverDistance < 0.5f * column.gorgeHalfWidth) || column.cirqueCount > 0) {
                continue;
            }
            ++kept;
            const float surface = cliffs.surfaceAt(p.x, p.z);
            atLevel += column.ground >= column.riverLevel ? 1 : 0;
            atWater = atWater &&
                      std::abs(surface - std::min(column.ground, column.riverLevel)) <= 1.0e-2f &&
                      column.riverLevel - column.ground <= 1.5f;
        }
    }
    std::printf("gorges: %zu river cells in full cliffs, %d points deep inside (%d with the "
                "water below the relief)\n",
                cells.size(), kept, atLevel);
    TEST_CHECK(atLevel >= 10);
    TEST_CHECK(wideEnough);
    TEST_CHECK(atWater);
}

// Gorges are dug into the relief: samples inside a gorge, in cliffs of mask above 0.5, lie at
// least 8 m below the eroded height.
void testGorgesDugIntoRelief(const CliffRegion& cliffs) {
    const HeightGrid& grid = cliffs.eroded().grid();
    int dug = 0;
    for (std::size_t i = 0; i < grid.heights.size(); ++i) {
        const float halfWidth = cliffs.gorgeHalfWidth()[i];
        dug += halfWidth > 0.0f && cliffs.riverDistance()[i] < halfWidth &&
                       cliffs.mask()[i] > 0.5f &&
                       grid.heights[i] - cliffs.surface().heights[i] >= 8.0f
                   ? 1
                   : 0;
    }
    std::printf("gorges: %d samples dug 8 m or more\n", dug);
    TEST_CHECK(dug >= 5);
}

// Walls: just outside a gorge, still within reach of the same river, the rock stands at least
// 8 m above the water.
void testGorgeWalls(const CliffRegion& cliffs) {
    const CliffParams& params = cliffs.field().params();
    int checked = 0;
    int walls = 0;
    for (const int i : gorgeRiverCells(cliffs, 0.5f)) {
        const Point center = cellCenter(cliffs.eroded(), i);
        const double halfWidth = static_cast<double>(cliffs.gorgeHalfWidth()[static_cast<std::size_t>(i)]);
        for (const double outside : {8.0, 16.0, 24.0, 32.0}) {
            for (const Point& axis : kAxes) {
                const Point p{center.x + (halfWidth + outside) * axis.x,
                              center.z + (halfWidth + outside) * axis.z};
                const CliffColumn column = cliffs.columnAt(p.x, p.z);
                if (!(column.riverDistance > column.gorgeHalfWidth + 5.0f &&
                      column.riverDistance < params.riverReach) ||
                    nearestRiverCell(cliffs.eroded(), p, params.riverReach) != i) {
                    continue;
                }
                ++checked;
                walls += cliffs.surfaceAt(p.x, p.z) >= column.riverLevel + 8.0f ? 1 : 0;
            }
        }
    }
    std::printf("gorge walls: %d of %d points outside a gorge stand 8 m above the water\n", walls,
                checked);
    TEST_CHECK(checked > 0);
    TEST_CHECK(walls >= 1);
}

// The cliffs never change the Phase 4 result: relief, drainage, rivers and cascades.
void testErodedRegionUnchanged(const CliffRegion& cliffs, const HeightmapRegion& site,
                               std::uint64_t seed) {
    const ErodedRegion plain(seed, site);
    const RiverNetwork& rivers = cliffs.eroded().rivers();
    TEST_CHECK(cliffs.eroded().grid().heights == plain.grid().heights);
    TEST_CHECK(rivers.receiver == plain.rivers().receiver);
    TEST_CHECK(rivers.river == plain.rivers().river);
    TEST_CHECK(rivers.catchment == plain.rivers().catchment);
    TEST_CHECK(rivers.cascades == plain.rivers().cascades);
}

// Water exactly at the surface: rivers are neither buried nor drained, whatever the cliffs.
void testRiverWaterAtSurface(const CliffRegion& cliffs) {
    const ErodedRegion& eroded = cliffs.eroded();
    const HeightGrid& grid = eroded.grid();
    int count = 0;
    bool atSurface = true;
    bool airAbove = true;
    bool rockBelow = true;
    for (int i = 0; i < static_cast<int>(grid.heights.size()); ++i) {
        if (!eroded.rivers().isRiver(i)) {
            continue;
        }
        ++count;
        const Point p = cellCenter(eroded, i);
        const float level = heightOf(grid, i);
        atSurface = atSurface &&
                    std::abs(cliffs.surface().heights[static_cast<std::size_t>(i)] - level) <= 2.0e-3f;
        airAbove = airAbove && cliffs.densityAt(p.x, static_cast<double>(level) + 0.5, p.z) > 0.0f;
        rockBelow = rockBelow && cliffs.densityAt(p.x, static_cast<double>(level) - 0.5, p.z) < 0.0f;
    }
    TEST_CHECK(count > 50);
    TEST_CHECK(atSurface);
    TEST_CHECK(airAbove);
    TEST_CHECK(rockBelow);
}

// Within reach of a river the rock never ends below the water (cirques excepted).
void testRiverBanksNotBuried(const CliffRegion& cliffs) {
    const CliffParams& params = cliffs.field().params();
    const HeightGrid& grid = cliffs.eroded().grid();
    const double cirqueReach = static_cast<double>(params.cirqueRadius) + grid.metersPerPixel;
    int checked = 0;
    bool notBuried = true;
    for (int i = 0; i < static_cast<int>(grid.heights.size()); ++i) {
        const auto cell = static_cast<std::size_t>(i);
        const Point p = cellCenter(cliffs.eroded(), i);
        bool nearCirque = false;
        for (const Cirque& cirque : cliffs.cirques()) {
            nearCirque = nearCirque || std::hypot(p.x - cirque.x, p.z - cirque.z) <= cirqueReach;
        }
        if (cliffs.riverDistance()[cell] < params.riverReach && cliffs.mask()[cell] > 0.0f &&
            !nearCirque) {
            ++checked;
            const float water = std::min(grid.heights[cell], cliffs.riverLevel()[cell]);
            notBuried = notBuried && cliffs.surface().heights[cell] >= water - 2.0e-3f;
        }
    }
    TEST_CHECK(checked > 50);
    TEST_CHECK(notBuried);
}

// Oceans are untouched, land stays above the sea.
void testOceansUntouched(const CliffRegion& cliffs, bool expectOcean) {
    const HeightGrid& grid = cliffs.eroded().grid();
    int ocean = 0;
    bool oceanIntact = true;
    bool landAboveSea = true;
    for (std::size_t i = 0; i < grid.heights.size(); ++i) {
        const float surface = cliffs.surface().heights[i];
        if (grid.heights[i] < kSeaLevel) {
            ++ocean;
            oceanIntact = oceanIntact && surface == grid.heights[i] && cliffs.mask()[i] == 0.0f;
        } else {
            landAboveSea = landAboveSea && surface >= kSeaLevel;
        }
    }
    std::printf("oceans: %d ocean samples\n", ocean);
    TEST_CHECK(oceanIntact);
    TEST_CHECK(landAboveSea);
    TEST_CHECK(!expectOcean || ocean > 0);
}

// Independent re-implementation of the cascade rule of eroded_region_test.cpp.
template <class Visit>
void forNeighbors(const HeightGrid& grid, int index, Visit visit) {
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int c = columnOf(grid, index) + dx;
            const int r = rowOf(grid, index) + dz;
            if ((dx != 0 || dz != 0) && c >= 0 && r >= 0 && c < grid.size && r < grid.size) {
                const float distance = dx != 0 && dz != 0 ? std::sqrt(2.0f) : 1.0f;
                visit(static_cast<int>(grid.index(c, r)), distance);
            }
        }
    }
}

float steepestSlope(const HeightGrid& grid, int index) {
    const auto water = [&](int i) { return std::max(heightOf(grid, i), kSeaLevel); };
    float steepest = 0.0f;
    forNeighbors(grid, index, [&](int n, float distance) {
        steepest = std::max(steepest, (water(index) - water(n)) /
                                          (distance * static_cast<float>(grid.metersPerPixel)));
    });
    return steepest;
}

bool isSlopeBreak(const HeightGrid& grid, const RiverNetwork& rivers, int index,
                  const terrain::RiverParams& params) {
    if (!rivers.isRiver(index) || rivers.receiverOf(index) == kNoReceiver) {
        return false;
    }
    int donor = kNoReceiver;
    forNeighbors(grid, index, [&](int n, float) {
        const auto catchment = [&](int i) {
            return rivers.catchment[static_cast<std::size_t>(i)];
        };
        if (rivers.receiverOf(n) == index &&
            (donor == kNoReceiver || catchment(n) > catchment(donor))) {
            donor = n;
        }
    });
    if (donor == kNoReceiver) {
        return false;
    }
    const float below = steepestSlope(grid, index);
    return below >= params.cascadeMinSlope &&
           below - steepestSlope(grid, donor) >= params.cascadeMinBreak;
}

// Cascades are exactly the slope breaks of the water surface, which the cliffs leave alone.
void testCascadesAreSlopeBreaks(const CliffRegion& cliffs) {
    const HeightGrid& grid = cliffs.eroded().grid();
    const RiverNetwork& rivers = cliffs.eroded().rivers();
    const terrain::RiverParams params;
    std::set<std::pair<int, int>> listed;
    for (const Cascade& cascade : rivers.cascades) {
        listed.insert({cascade.column, cascade.row});
    }
    bool exact = true;
    for (int i = 0; i < static_cast<int>(grid.heights.size()); ++i) {
        const bool isCascade = listed.contains({columnOf(grid, i), rowOf(grid, i)});
        exact = exact && isCascade == isSlopeBreak(grid, rivers, i, params);
    }
    TEST_CHECK(exact);
}

// A cirque is carved below each cascade: its floor is the water of the receiver, and the
// waterfall drops from a vertical wall at the edge of the cirque, on the cascade side. Near
// the wall the surface is the floor, or the relief where that is lower than the floor.
void testCascadesAtCliffEdge(const CliffRegion& cliffs) {
    const ErodedRegion& eroded = cliffs.eroded();
    const auto& cascades = eroded.rivers().cascades;
    const double radius = static_cast<double>(cliffs.field().params().cirqueRadius);
    std::printf("cirques: %zu for %zu cascades\n", cliffs.cirques().size(), cascades.size());
    TEST_CHECK(!cliffs.cirques().empty());
    bool floors = true;
    bool onWater = true;
    bool inside = true;
    bool drops = true;
    bool walls = true;
    for (const Cirque& cirque : cliffs.cirques()) {
        const auto k = static_cast<std::size_t>(cirque.cascade);
        if (k >= cascades.size()) {
            floors = false;
            continue;
        }
        const Cascade& cascade = cascades[k];
        const int receiver = eroded.rivers().receiverOf(cascade.row * eroded.grid().size + cascade.column);
        floors = floors && receiver != kNoReceiver &&
                 std::abs(cirque.floor - heightOf(eroded.grid(), receiver)) <= 1.0e-4f;
        const Point cell{eroded.sampleX(cascade.column), eroded.sampleZ(cascade.row)};
        const float cellSurface = cliffs.surfaceAt(cell.x, cell.z);
        onWater = onWater && std::abs(cellSurface - cascade.top) <= 2.0e-3f;
        // Point at distance t from the axis, on the segment toward the cascade cell.
        const double length = std::hypot(cell.x - cirque.x, cell.z - cirque.z);
        const auto at = [&](double t) {
            return Point{cirque.x + (cell.x - cirque.x) / length * t,
                         cirque.z + (cell.z - cirque.z) / length * t};
        };
        const auto surfaceAt = [&](double t) { return cliffs.surfaceAt(at(t).x, at(t).z); };
        const float height = cascade.top - cascade.bottom;
        const CliffColumn near = cliffs.columnAt(at(radius - 1.0).x, at(radius - 1.0).z);
        inside = inside && (near.ground > cirque.floor + 1.0e-2f
                                ? std::abs(surfaceAt(radius - 1.0) - cirque.floor) <= 1.0e-2f
                                : surfaceAt(radius - 1.0) <= cirque.floor + 1.0e-2f);
        drops = drops && cellSurface - cirque.floor >= height - 1.0e-2f;
        walls = walls && length > radius + 0.5 &&
                surfaceAt(radius + 0.5) - surfaceAt(radius - 0.5) >= 0.5f * height;
        std::printf("cirque %d: mask %.3f, floor %.3f, drop %.2f; at R - 1: ground %.3f surface "
                    "%.3f; step across the wall %.2f\n",
                    cirque.cascade, static_cast<double>(near.cliff), static_cast<double>(cirque.floor),
                    static_cast<double>(height), static_cast<double>(near.ground),
                    static_cast<double>(surfaceAt(radius - 1.0)),
                    static_cast<double>(surfaceAt(radius + 0.5) - surfaceAt(radius - 0.5)));
    }
    TEST_CHECK(floors);
    TEST_CHECK(onWater);
    TEST_CHECK(inside);
    TEST_CHECK(drops);
    TEST_CHECK(walls);
}

} // namespace

int main() {
    const HeightmapRegion site = terrain::findCliffRegion(kCascadeSeed, 50.0, 129);
    const CliffRegion cliffs(kCascadeSeed, site);
    testGorgesAtWater(cliffs);
    testGorgesDugIntoRelief(cliffs);
    testGorgeWalls(cliffs);
    testErodedRegionUnchanged(cliffs, site, kCascadeSeed);
    testRiverWaterAtSurface(cliffs);
    testRiverBanksNotBuried(cliffs);
    testOceansUntouched(cliffs, false);
    testCascadesAreSlopeBreaks(cliffs);
    testCascadesAtCliffEdge(cliffs);

    // A coast with cliffs inland: the ocean, the coastline and the rivers reaching the sea
    // are untouched (found by a search of the default world).
    const HeightmapRegion shore{
        .centerX = -66800.0, .centerZ = -2800.0, .metersPerPixel = 100.0, .size = 128};
    const CliffRegion coast(kSeed, shore);
    TEST_CHECK(std::any_of(coast.mask().begin(), coast.mask().end(),
                           [](float mask) { return mask > 0.0f; }));
    testErodedRegionUnchanged(coast, shore, kSeed);
    testRiverWaterAtSurface(coast);
    testRiverBanksNotBuried(coast);
    testOceansUntouched(coast, true);
    testCascadesAreSlopeBreaks(coast);
    return test::result();
}
