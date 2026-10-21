// Cliffs of a region seen from the volume: determinism, vertical walls, flat plateaus and
// continuity with the eroded relief. Gorges, rivers, cascades and cirques are checked in
// cliff_rivers_test.cpp. Every parameter is read from the field, nothing is hard-coded.
#include "terrain/generation/cliff_region.hpp"
#include "terrain/generation/noise.hpp"

#include "legacy_relief_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::CliffColumn;
using terrain::CliffField;
using terrain::CliffRegion;
using terrain::HeightGrid;
using terrain::HeightmapRegion;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

struct Point {
    double x;
    double z;
};

// Deterministic pseudo-random numbers in [0, 1): the stream is a seed, each call a counter.
class Rng {
public:
    explicit Rng(std::uint64_t stream) : stream_(stream) {}

    double next() {
        const std::uint64_t bits = terrain::noise::deriveSeed(stream_, counter_++);
        return static_cast<double>(bits >> 40) / 16777216.0;
    }
    double range(double low, double high) { return low + (high - low) * next(); }

private:
    std::uint64_t stream_;
    std::uint64_t counter_ = 0;
};

double halfExtent(const HeightmapRegion& region) {
    return region.size * region.metersPerPixel / 2.0;
}

// Random point of the square `scale` times as large as the region, around its center.
Point randomPoint(const HeightmapRegion& region, Rng& rng, double scale) {
    const double half = scale * halfExtent(region);
    return {region.centerX + rng.range(-half, half), region.centerZ + rng.range(-half, half)};
}

// A column deep in the cliffs (mask exactly 1) and out of reach of the rivers: strata only.
bool isPlainCliff(const CliffRegion& cliffs, const Point& p) {
    const CliffColumn column = cliffs.columnAt(p.x, p.z);
    return column.cliff == 1.0f && column.riverDistance >= cliffs.field().params().riverReach;
}

// Two plain cliff columns 0.5 m apart and their surfaces.
struct SurfacePair {
    Point first;
    Point second;
    float firstSurface;
    float secondSurface;
};

constexpr double kPairDistance = 0.5;

std::vector<SurfacePair> collectPairs(const CliffRegion& cliffs, std::size_t count) {
    const HeightmapRegion& region = cliffs.eroded().region();
    Rng rng(101);
    std::vector<SurfacePair> pairs;
    for (std::size_t attempt = 0; pairs.size() < count && attempt < 20 * count; ++attempt) {
        const Point a = randomPoint(region, rng, 1.0);
        const bool alongX = rng.next() < 0.5;
        const Point b{a.x + (alongX ? kPairDistance : 0.0), a.z + (alongX ? 0.0 : kPairDistance)};
        if (isPlainCliff(cliffs, a) && isPlainCliff(cliffs, b)) {
            pairs.push_back({a, b, cliffs.surfaceAt(a.x, a.z), cliffs.surfaceAt(b.x, b.z)});
        }
    }
    return pairs;
}

// ---------------------------------------------------------------------------

// Same seed, same region: same grids, cirques and densities. Another seed: another world.
void testDeterminism(const CliffRegion& a, const HeightmapRegion& site) {
    const CliffRegion b(kSeed, site, test_relief::onLegacyRelief(terrain::CliffRegionParams{}));
    TEST_CHECK(a.surface().heights == b.surface().heights);
    TEST_CHECK(a.mask() == b.mask());
    TEST_CHECK(a.riverDistance() == b.riverDistance());
    TEST_CHECK(a.riverLevel() == b.riverLevel());
    TEST_CHECK(a.gorgeHalfWidth() == b.gorgeHalfWidth());
    TEST_CHECK(a.cirques() == b.cirques());

    Rng rng(1);
    bool same = true;
    for (int k = 0; k < 600; ++k) {
        const Point p = randomPoint(site, rng, 1.0);
        const double y = a.eroded().heightAt(p.x, p.z) + rng.range(-60.0, 60.0);
        same = same && a.densityAt(p.x, y, p.z) == b.densityAt(p.x, y, p.z);
    }
    TEST_CHECK(same);

    const HeightmapRegion small =
        terrain::findCliffRegion(kSeed, 100.0, 48, {}, test_relief::legacyRelief());
    const CliffRegion c(kSeed, small, test_relief::onLegacyRelief(terrain::CliffRegionParams{}));
    const CliffRegion d(kSeed + 1, small,
                        test_relief::onLegacyRelief(terrain::CliffRegionParams{}));
    TEST_CHECK(c.mask() != d.mask() || c.surface().heights != d.surface().heights);
}

// Where the surface jumps by half a stratum or more within 0.5 m the wall is vertical in the
// volume: at five heights between the two surfaces, rock on the high side and air on the low.
void testVerticalCliffs(const CliffRegion& cliffs, const std::vector<SurfacePair>& pairs) {
    const float minWall = cliffs.field().params().stratumHeight / 2.0f;
    int walls = 0;
    bool vertical = true;
    for (const SurfacePair& pair : pairs) {
        if (std::abs(pair.firstSurface - pair.secondSurface) < minWall) {
            continue;
        }
        ++walls;
        const bool firstIsHigh = pair.firstSurface > pair.secondSurface;
        const Point high = firstIsHigh ? pair.first : pair.second;
        const Point low = firstIsHigh ? pair.second : pair.first;
        const double top = std::max(pair.firstSurface, pair.secondSurface);
        const double bottom = std::min(pair.firstSurface, pair.secondSurface);
        for (int i = 1; i <= 5; ++i) {
            const double y = bottom + (top - bottom) * i / 6.0;
            vertical = vertical && cliffs.densityAt(high.x, y, high.z) < 0.0f &&
                       cliffs.densityAt(low.x, y, low.z) > 0.0f;
        }
    }
    std::printf("vertical cliffs: %d walls among %zu pairs\n", walls, pairs.size());
    TEST_CHECK(walls > 10);
    TEST_CHECK(vertical);
}

// Plateaus have flat treads at the top of a stratum, and their rims are sharp: 0.5 m apart the
// surface is level or steps up by a wall, almost never a slope.
void testPlateausWithSharpRims(const CliffRegion& cliffs, const std::vector<SurfacePair>& pairs) {
    const CliffField& field = cliffs.field();
    const float minWall = field.params().stratumHeight / 2.0f;
    const auto onTread = [&](float surface) {
        const float top = field.stratumTop(field.stratumBelow(surface + 1.0e-2f));
        return std::abs(surface - top) <= 1.0e-2f;
    };
    int points = 0;
    int treads = 0;
    int levelOrWall = 0;
    for (const SurfacePair& pair : pairs) {
        points += 2;
        treads += (onTread(pair.firstSurface) ? 1 : 0) + (onTread(pair.secondSurface) ? 1 : 0);
        const float step = std::abs(pair.firstSurface - pair.secondSurface);
        levelOrWall += step < 0.05f || step >= minWall ? 1 : 0;
    }
    std::printf("plateaus: %d of %d points on a tread, %d of %zu pairs level or wall\n", treads,
                points, levelOrWall, pairs.size());
    TEST_CHECK(points >= 1000);
    TEST_CHECK(treads * 100 >= points * 99);
    TEST_CHECK(levelOrWall * 100 >= static_cast<int>(pairs.size()) * 99);
}

// Points beyond the region square, a strip inside it where the border weight is 0, and any
// random interior column whose mask is 0.
std::vector<Point> maskZeroPoints(const CliffRegion& cliffs) {
    const HeightmapRegion& region = cliffs.eroded().region();
    const double half = halfExtent(region);
    std::vector<Point> points;
    for (const double distance : {1.0, 10.0, 100.0, 1000.0, 20000.0}) {
        points.push_back({region.centerX + half + distance, region.centerZ + 0.3 * half});
        points.push_back({region.centerX - half - distance, region.centerZ - 0.7 * half});
        points.push_back({region.centerX + 0.1 * half, region.centerZ + half + distance});
        points.push_back({region.centerX - 0.4 * half, region.centerZ - half - distance});
        points.push_back({region.centerX + half + distance, region.centerZ - half - distance});
    }
    // Inside the region, between the outer samples and its edge: a quarter cell from the edge.
    const double edge = half - 0.25 * region.metersPerPixel;
    for (const double t : {-0.8, -0.2, 0.3, 0.9}) {
        points.push_back({region.centerX + edge, region.centerZ + t * half});
        points.push_back({region.centerX - edge, region.centerZ + t * half});
        points.push_back({region.centerX + t * half, region.centerZ + edge});
        points.push_back({region.centerX + t * half, region.centerZ - edge});
    }
    Rng rng(5);
    int found = 0;
    for (int attempt = 0; attempt < 4000 && found < 100; ++attempt) {
        const Point p = randomPoint(region, rng, 1.0);
        if (cliffs.columnAt(p.x, p.z).cliff == 0.0f) {
            points.push_back(p);
            ++found;
        }
    }
    return points;
}

// Beyond the region and wherever the mask is 0 the density is exactly the eroded relief.
void testExactReliefWhereMaskIsZero(const CliffRegion& cliffs) {
    bool exact = true;
    bool masked = true;
    const std::vector<Point> points = maskZeroPoints(cliffs);
    for (const Point& p : points) {
        const float ground = cliffs.eroded().heightAt(p.x, p.z);
        masked = masked && cliffs.columnAt(p.x, p.z).cliff == 0.0f;
        exact = exact && cliffs.surfaceAt(p.x, p.z) == ground;
        for (const double offset : {-300.0, -5.0, 0.0, 0.5, 5.0, 300.0}) {
            const double y = static_cast<double>(ground) + offset;
            exact = exact && cliffs.densityAt(p.x, y, p.z) ==
                                 static_cast<float>(y - cliffs.eroded().heightAt(p.x, p.z));
        }
    }
    TEST_CHECK(masked);
    TEST_CHECK(exact);
    TEST_CHECK(points.size() > 50);
}

// On the border ring the cliffs are off: the surface is the eroded grid.
void testBorderRing(const CliffRegion& cliffs) {
    const HeightGrid& grid = cliffs.eroded().grid();
    int ring = 0;
    bool flat = true;
    for (int row = 0; row < grid.size; ++row) {
        for (int column = 0; column < grid.size; ++column) {
            if (column != 0 && row != 0 && column != grid.size - 1 && row != grid.size - 1) {
                continue;
            }
            const std::size_t i = grid.index(column, row);
            ++ring;
            flat = flat && std::abs(cliffs.surface().heights[i] - grid.heights[i]) <= 2.0e-3f &&
                   cliffs.mask()[i] == 0.0f;
        }
    }
    TEST_CHECK(ring == 4 * (grid.size - 1));
    TEST_CHECK(flat);
}

// No jump of the density near the surface, the region boundary included: a step of 1 mm along
// x, y or z changes it by at most maxSlope * step. The constant 60 covers the relief slope
// (the eroded relief climbs to about 20), the footprint slope 1 / minSlope of a stratum seen
// from a flat column, and the variations of the mask and of the rim recession.
void testNoJumpNearSurface(const CliffRegion& cliffs) {
    constexpr double kStep = 1.0e-3;
    constexpr double kMaxSlope = 60.0;
    constexpr double kSlack = 2.0e-3; // rounding of float heights of about 2000 m
    const HeightmapRegion& region = cliffs.eroded().region();
    Rng rng(9);
    double worst = 0.0;
    bool continuous = true;
    for (int k = 0; k < 20000; ++k) {
        const Point p = randomPoint(region, rng, 1.1);
        const double y = cliffs.surfaceAt(p.x, p.z) + rng.range(-10.0, 10.0);
        const float here = cliffs.densityAt(p.x, y, p.z);
        const float shifted[3] = {cliffs.densityAt(p.x + kStep, y, p.z),
                                  cliffs.densityAt(p.x, y + kStep, p.z),
                                  cliffs.densityAt(p.x, y, p.z + kStep)};
        for (const float value : shifted) {
            const double change = std::abs(static_cast<double>(value) - static_cast<double>(here));
            worst = std::max(worst, change / kStep);
            continuous = continuous && std::isfinite(value) && change <= kMaxSlope * kStep + kSlack;
        }
    }
    std::printf("no jump: largest |change| / step = %.2f (limit %.0f)\n", worst, kMaxSlope);
    TEST_CHECK(continuous);
}

// The cliffs only cut, by at most one terrace drop, except in gorges and cirques: mountains
// and valleys keep their shape, and the highest summit stays.
void testBoundedChange(const CliffRegion& cliffs) {
    const HeightGrid& grid = cliffs.eroded().grid();
    const float cell = static_cast<float>(grid.metersPerPixel);
    const float drop = cliffs.field().maxTerraceDrop();
    const float cirqueReach = cliffs.field().params().cirqueRadius + cell;
    int checked = 0;
    bool bounded = true;
    for (int row = 0; row < grid.size; ++row) {
        for (int column = 0; column < grid.size; ++column) {
            const std::size_t i = grid.index(column, row);
            const float halfWidth = cliffs.gorgeHalfWidth()[i];
            bool excluded = halfWidth > 0.0f && cliffs.riverDistance()[i] <= halfWidth + cell;
            for (const terrain::Cirque& cirque : cliffs.cirques()) {
                excluded = excluded || std::hypot(cliffs.eroded().sampleX(column) - cirque.x,
                                                  cliffs.eroded().sampleZ(row) - cirque.z) <=
                                           static_cast<double>(cirqueReach);
            }
            if (!excluded) {
                ++checked;
                const float surface = cliffs.surface().heights[i];
                bounded = bounded && surface >= grid.heights[i] - drop - 2.0e-3f &&
                          surface <= grid.heights[i] + 2.0e-3f;
            }
        }
    }
    TEST_CHECK(checked > static_cast<int>(grid.heights.size()) / 4);
    TEST_CHECK(bounded);
    const float top = *std::max_element(grid.heights.begin(), grid.heights.end());
    const auto& surface = cliffs.surface().heights;
    TEST_CHECK(*std::max_element(surface.begin(), surface.end()) >= top - drop);
}

} // namespace

int main() {
    const HeightmapRegion site =
        terrain::findCliffRegion(kSeed, 50.0, 129, {}, test_relief::legacyRelief());
    const CliffRegion cliffs(kSeed, site,
                             test_relief::onLegacyRelief(terrain::CliffRegionParams{}));
    const std::vector<SurfacePair> pairs = collectPairs(cliffs, 16000);
    testDeterminism(cliffs, site);
    testVerticalCliffs(cliffs, pairs);
    testPlateausWithSharpRims(cliffs, pairs);
    testExactReliefWhereMaskIsZero(cliffs);
    testBorderRing(cliffs);
    testNoJumpNearSurface(cliffs);
    testBoundedChange(cliffs);
    return test::result();
}
