#include "terrain/generation/eroded_region.hpp"
#include "terrain/generation/noise.hpp"

#include "legacy_relief_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>

namespace {

using terrain::ErodedRegion;
using terrain::HeightGrid;
using terrain::HeightmapRegion;
using terrain::kNoReceiver;
using terrain::kSeaLevel;
using terrain::RiverNetwork;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

// Highest point of a coarse search around the origin: the heart of a mountain range.
HeightmapRegion mountainRegion() {
    const terrain::Heightfield field(kSeed, test_relief::legacyRelief());
    HeightmapRegion region{.metersPerPixel = 80.0, .size = 128};
    float highest = std::numeric_limits<float>::lowest();
    for (int j = -20; j <= 20; ++j) {
        for (int i = -20; i <= 20; ++i) {
            const float h = field.heightAt(i * 1000.0, j * 1000.0);
            if (h > highest) {
                highest = h;
                region.centerX = i * 1000.0;
                region.centerZ = j * 1000.0;
            }
        }
    }
    return region;
}

// Coast closest to the origin along a coarse search: ocean to the west, land to the east.
HeightmapRegion coastRegion() {
    const terrain::Heightfield field(kSeed, test_relief::legacyRelief());
    for (int radius = 0; radius <= 30; ++radius) {
        for (int j = -radius; j <= radius; ++j) {
            for (int i = -radius; i <= radius; ++i) {
                const double x = i * 1000.0;
                const double z = j * 1000.0;
                if (std::max(std::abs(i), std::abs(j)) == radius &&
                    field.heightAt(x, z) < kSeaLevel &&
                    field.heightAt(x + 1000.0, z) >= kSeaLevel) {
                    return {.centerX = x, .centerZ = z, .metersPerPixel = 100.0, .size = 128};
                }
            }
        }
    }
    return {};
}

int columnOf(const HeightGrid& grid, int index) {
    return index % grid.size;
}

int rowOf(const HeightGrid& grid, int index) {
    return index / grid.size;
}

bool isBorder(const HeightGrid& grid, int index) {
    const int column = columnOf(grid, index);
    const int row = rowOf(grid, index);
    return column == 0 || row == 0 || column == grid.size - 1 || row == grid.size - 1;
}

float heightOf(const HeightGrid& grid, int index) {
    return grid.heights[static_cast<std::size_t>(index)];
}

// Visits the existing 8 neighbors of a cell: visit(neighbor index, distance in cells).
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

// Steepest slope between neighbors (rise over run).
double maxNeighborSlope(const HeightGrid& grid) {
    double steepest = 0.0;
    for (int i = 0; i < static_cast<int>(grid.heights.size()); ++i) {
        forNeighbors(grid, i, [&](int n, float distance) {
            const double rise =
                std::abs(static_cast<double>(heightOf(grid, n)) - heightOf(grid, i));
            steepest = std::max(steepest, rise / distance);
        });
    }
    return steepest / grid.metersPerPixel;
}

// Depth of a cell below the mean of the 7 x 7 cells around it, in meters.
double incision(const HeightGrid& grid, int column, int row) {
    double sum = 0.0;
    for (int dz = -3; dz <= 3; ++dz) {
        for (int dx = -3; dx <= 3; ++dx) {
            sum += grid.at(column + dx, row + dz);
        }
    }
    return sum / 49.0 - grid.at(column, row);
}

// ---------------------------------------------------------------------------

void testDeterminism(const HeightmapRegion& region) {
    const ErodedRegion a(kSeed, region, test_relief::onLegacyRelief(terrain::ErodedRegionParams{}));
    const ErodedRegion b(kSeed, region, test_relief::onLegacyRelief(terrain::ErodedRegionParams{}));
    TEST_CHECK(a.grid().heights == b.grid().heights);
    TEST_CHECK(a.rivers().receiver == b.rivers().receiver);
    TEST_CHECK(a.rivers().catchment == b.rivers().catchment);
    TEST_CHECK(a.rivers().river == b.rivers().river);
    TEST_CHECK(a.rivers().cascades == b.rivers().cascades);

    // Another seed erodes the same relief differently.
    terrain::ErodedRegionParams params;
    params.erosion.dropletsPerCell = 0.5f;
    const ErodedRegion c(kSeed, region, test_relief::onLegacyRelief(params));
    TEST_CHECK(c.rawGrid().heights == a.rawGrid().heights);
    TEST_CHECK(c.grid().heights != a.grid().heights);
    const ErodedRegion other(kSeed + 1, region,
                             test_relief::onLegacyRelief(terrain::ErodedRegionParams{}));
    TEST_CHECK(other.grid().heights != a.grid().heights);
}

void testContinuity(const ErodedRegion& eroded) {
    const HeightmapRegion& region = eroded.region();
    const HeightGrid& grid = eroded.grid();
    // At the samples, heightAt gives the eroded grid; outside, the original heightfield.
    bool samples = true;
    for (int row = 0; row < grid.size; row += 7) {
        for (int column = 0; column < grid.size; column += 5) {
            const float h = eroded.heightAt(eroded.sampleX(column), eroded.sampleZ(row));
            samples = samples && std::abs(h - grid.at(column, row)) <= 2.0e-3f;
        }
    }
    TEST_CHECK(samples);
    const double half = region.size * region.metersPerPixel / 2.0;
    const double outsideX = region.centerX + half + 1.0;
    TEST_CHECK(eroded.heightAt(outsideX, region.centerZ) ==
               eroded.field().heightAt(outsideX, region.centerZ));

    // No jump anywhere, the region boundary included: points cover a square 10% larger.
    constexpr double kMaxSlope = 20.0;
    constexpr double kStep = 0.01;
    constexpr double kTolerance = kMaxSlope * kStep + 2.0e-3;
    bool continuous = true;
    for (std::uint64_t k = 0; k < 20000; ++k) {
        const std::uint64_t bits = terrain::noise::deriveSeed(99, k);
        const double u = static_cast<double>(bits >> 40) / 16777216.0 - 0.5;
        const double v = static_cast<double>((bits >> 16) & 0xFFFFFF) / 16777216.0 - 0.5;
        const double x = region.centerX + 2.2 * half * u;
        const double z = region.centerZ + 2.2 * half * v;
        const float h = eroded.heightAt(x, z);
        continuous = continuous && std::isfinite(h) &&
                     std::abs(eroded.heightAt(x + kStep, z) - h) <= kTolerance &&
                     std::abs(eroded.heightAt(x, z + kStep) - h) <= kTolerance;
    }
    TEST_CHECK(continuous);
    // Erosion does not create steps: gullies may steepen up to the talus slope, never beyond
    // the steepest slope of the original relief.
    const double talus = terrain::ErosionParams{}.talusSlope;
    TEST_CHECK(maxNeighborSlope(grid) <= std::max(maxNeighborSlope(eroded.rawGrid()), talus));
}

// River valleys are more deeply cut into their surroundings than before erosion.
void testValleysDeeper(const ErodedRegion& eroded) {
    const HeightGrid& grid = eroded.grid();
    double before = 0.0;
    double after = 0.0;
    int count = 0;
    for (int row = 3; row < grid.size - 3; ++row) {
        for (int column = 3; column < grid.size - 3; ++column) {
            if (eroded.rivers().isRiver(static_cast<int>(grid.index(column, row)))) {
                before += incision(eroded.rawGrid(), column, row);
                after += incision(grid, column, row);
                ++count;
            }
        }
    }
    TEST_CHECK(count > 50);
    TEST_CHECK(after / count > before / count + 2.0);
}

// Finite heights within the original range, and no hole: every land cell inside the region
// has a neighbor at or below it, so water never gets trapped.
void testNoHolesOrInvalidValues(const ErodedRegion& eroded) {
    const HeightGrid& grid = eroded.grid();
    const auto [low, high] =
        std::minmax_element(eroded.rawGrid().heights.begin(), eroded.rawGrid().heights.end());
    bool valid = grid.heights.size() == eroded.rawGrid().heights.size();
    bool noHole = true;
    for (int i = 0; i < static_cast<int>(grid.heights.size()); ++i) {
        const float h = heightOf(grid, i);
        valid = valid && std::isfinite(h) && h >= *low && h <= *high;
        if (h < kSeaLevel || isBorder(grid, i)) {
            continue;
        }
        float lowest = std::numeric_limits<float>::max();
        forNeighbors(grid, i, [&](int n, float) { lowest = std::min(lowest, heightOf(grid, n)); });
        noHole = noHole && lowest <= h;
    }
    TEST_CHECK(valid);
    TEST_CHECK(noHole);
}

// The land and ocean masks are unchanged, oceans are untouched and summits remain.
void testContinentsPreserved(const ErodedRegion& eroded, bool expectOcean) {
    const HeightGrid& raw = eroded.rawGrid();
    const HeightGrid& grid = eroded.grid();
    bool sameCoast = true;
    int ocean = 0;
    for (std::size_t i = 0; i < raw.heights.size(); ++i) {
        const bool wasOcean = raw.heights[i] < kSeaLevel;
        sameCoast = sameCoast && (wasOcean ? grid.heights[i] == raw.heights[i]
                                           : grid.heights[i] >= kSeaLevel);
        ocean += wasOcean ? 1 : 0;
    }
    TEST_CHECK(sameCoast);
    const auto total = static_cast<int>(raw.heights.size());
    TEST_CHECK(!expectOcean || (ocean > total / 20 && ocean < total * 19 / 20));
    const float rawTop = *std::max_element(raw.heights.begin(), raw.heights.end());
    const float top = *std::max_element(grid.heights.begin(), grid.heights.end());
    TEST_CHECK(rawTop <= kSeaLevel || top >= 0.9f * rawTop);
}

// Each river cell flows to a river cell or into the ocean, never uphill, until an outlet.
void testRiversConnected(const ErodedRegion& eroded, bool expectMouth) {
    const HeightGrid& grid = eroded.grid();
    const RiverNetwork& rivers = eroded.rivers();
    int count = 0;
    int mouths = 0;
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
            const bool ocean = heightOf(grid, next) < kSeaLevel;
            connected = connected && (rivers.isRiver(next) || ocean) &&
                        heightOf(grid, next) <= heightOf(grid, cell);
            mouths += ocean && cell == i ? 1 : 0;
            cell = next;
        }
        connected = connected && (heightOf(grid, cell) < kSeaLevel || isBorder(grid, cell));
    }
    TEST_CHECK(connected);
    TEST_CHECK(count > 50);
    TEST_CHECK(!expectMouth || mouths > 0);
}

// Independent re-implementation of the cascade rule, from the grid and the drainage.
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

// Cascades are exactly the river cells at a break in slope.
void testCascadesOnlyAtSlopeBreaks(const ErodedRegion& eroded) {
    const HeightGrid& grid = eroded.grid();
    const RiverNetwork& rivers = eroded.rivers();
    const terrain::RiverParams params;
    std::set<std::pair<int, int>> listed;
    bool consistent = true;
    for (const terrain::Cascade& cascade : rivers.cascades) {
        listed.insert({cascade.column, cascade.row});
        consistent = consistent && cascade.slopeBelow >= params.cascadeMinSlope &&
                     cascade.slopeBelow - cascade.slopeAbove >= params.cascadeMinBreak &&
                     cascade.top >= cascade.bottom;
    }
    bool exact = true;
    for (int i = 0; i < static_cast<int>(grid.heights.size()); ++i) {
        const bool isCascade = listed.contains({columnOf(grid, i), rowOf(grid, i)});
        exact = exact && isCascade == isSlopeBreak(grid, rivers, i, params);
    }
    TEST_CHECK(consistent);
    TEST_CHECK(exact);
    TEST_CHECK(!rivers.cascades.empty());
}

} // namespace

int main() {
    const HeightmapRegion mountain = mountainRegion();
    testDeterminism(mountain);
    const ErodedRegion mountains(kSeed, mountain,
                                 test_relief::onLegacyRelief(terrain::ErodedRegionParams{}));
    const ErodedRegion coast(kSeed, coastRegion(),
                             test_relief::onLegacyRelief(terrain::ErodedRegionParams{}));
    for (const ErodedRegion* eroded : {&mountains, &coast}) {
        testContinuity(*eroded);
        testNoHolesOrInvalidValues(*eroded);
        testContinentsPreserved(*eroded, eroded == &coast);
        testRiversConnected(*eroded, eroded == &coast);
    }
    testValleysDeeper(mountains);
    testCascadesOnlyAtSlopeBreaks(mountains);
    return test::result();
}
