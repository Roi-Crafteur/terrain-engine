// The boundaries of the plates of the geography (geography_boundaries.hpp, phase 11R bis): the
// warp that bends them never folds the plane; the draws of the boundaries (collisions without a
// chain, mostly weak ones; widths of the belts, wider foothills; plateaus only behind chains;
// depths of the rifts); the ranges of a belt end gradually, never in a wall; and the rifts of the
// default world (relief_rifts.hpp): their floor 1,500 to 2,500 m below their rim, flat, between
// steep walls.
#include "geography_boundaries.hpp"
#include "geography_grid.hpp"
#include "geography_stages.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/io/relief_rifts.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

namespace geography = terrain::geography;
using terrain::GeographyParams;
using terrain::WorldSize;

constexpr std::uint64_t kSeed = 0x6E06'7EC7ULL;

struct Stages {
    GeographyParams params;
    WorldSize world;
    geography::Grid grid;
    geography::Crust crust;
    std::uint64_t stream;
    terrain::TectonicPlates plates;
    geography::BoundaryFields fields;

    Stages()
        : grid(geography::makeGrid(world, params.cellSize)),
          crust(geography::buildCrust(kSeed, params, world, grid, 0)),
          stream(terrain::noise::deriveSeed(kSeed, geography::kPlates)),
          plates(terrain::noise::deriveSeed(stream, 0), params.plates, world),
          fields(stream, plates, grid, crust, 0) {}
};

// The warped node positions keep their orientation: the Jacobian of the map of the nodes to
// their warped positions (differences to the next node along each axis) stays positive.
void testBendDoesNotFold(const Stages& s) {
    double lowest = 1.0e9;
    for (int j = 0; j < s.grid.nz; ++j) {
        for (int i = 0; i < s.grid.nx; ++i) {
            const auto here = s.fields.at(i, j).warped;
            const auto east = s.fields.at(i + 1, j).warped;
            const auto south = s.fields.at(i, j + 1).warped;
            const double det = (east.x - here.x) * (south.z - here.z) -
                               (east.z - here.z) * (south.x - here.x);
            lowest = std::min(lowest, det / (s.grid.sizeX * s.grid.sizeZ));
        }
    }
    std::printf("lowest Jacobian of the warp of the boundaries: %.3f\n", lowest);
    TEST_CHECK(lowest > 0.0);
}

void testDraws(const Stages& s) {
    const std::vector<geography::Boundary> boundaries = geography::drawBoundaries(
        terrain::noise::deriveSeed(s.stream, 3), s.params, s.plates,
        geography::landShares(s.plates.plates().size(), s.fields.nearest(), s.fields.land()));
    int collisions = 0;
    int chainless = 0;
    double strengthChainless = 0.0;
    double strengthChains = 0.0;
    bool widths = true;
    bool rifts = true;
    for (const geography::Boundary& b : boundaries) {
        if (b.kind == terrain::BoundaryKind::Collision) {
            ++collisions;
            chainless += b.chainless ? 1 : 0;
            (b.chainless ? strengthChainless : strengthChains) += static_cast<double>(b.strength);
            const double share = b.halfWidth / s.params.beltWidth / (b.chainless ? 2.5 : 1.0);
            widths = widths && share >= 0.25 - 1e-9 && share <= 2.0 + 1e-9 &&
                     (!b.chainless || b.plateauWidth == 0.0);
        }
        rifts = rifts && b.riftDepth >= s.params.riftDepthLow &&
                b.riftDepth <= s.params.riftDepthHigh;
    }
    const double share = static_cast<double>(chainless) / collisions;
    std::printf("%d collisions, %.0f%% without a chain\n", collisions, 100.0 * share);
    TEST_CHECK(collisions > 20);
    TEST_CHECK(share >= 0.1 && share <= 0.4);
    TEST_CHECK(chainless > 0 && strengthChainless / chainless <
                                    strengthChains / (collisions - chainless));
    TEST_CHECK(widths);
    TEST_CHECK(rifts);
}

// Along the map, the share of a belt kept by its ranges changes by less than 0.6 over two nodes
// (130 km) at 99 % of the pairs: the ranges end in passes and gaps, not in walls.
void testRangesEndGradually(const Stages& s) {
    std::vector<double> changes;
    for (int j = 0; j < s.grid.nz; j += 3) {
        for (int i = 0; i < s.grid.nx; ++i) {
            const auto factor = [&s](int a, int b) {
                return geography::rangesFactor(s.fields.at(a, b).ranges);
            };
            changes.push_back(std::abs(factor(i + 2, j) - factor(i, j)));
            changes.push_back(std::abs(factor(i, j + 2) - factor(i, j)));
        }
    }
    std::sort(changes.begin(), changes.end());
    const double p99 = changes[changes.size() * 99 / 100];
    std::printf("ranges: 99th percentile of the change over 130 km %.2f\n", p99);
    TEST_CHECK(p99 < 0.6);
}

// The largest rifts of the default world: at least 80 % of them 1,500 to 2,500 m deep below
// their lower rim, their floor above landMin; the largest flat at its center (less than 10 % of
// slope over 10 km) between walls steeper than 30 degrees.
void testRifts() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{});
    const std::vector<terrain::RiftProfile> rifts = terrain::measureRifts(field, 8);
    TEST_CHECK(rifts.size() >= 5);
    int deep = 0;
    for (const terrain::RiftProfile& r : rifts) {
        std::printf("rift of %d nodes: floor %.0f m, rims %.0f and %.0f m, depth %.0f m, %.0f km "
                    "between the rims\n",
                    r.nodes, static_cast<double>(r.floor), static_cast<double>(r.rimLow),
                    static_cast<double>(r.rimHigh), r.depth(), r.width / 1000.0);
        deep += r.depth() >= 1500.0 && r.depth() <= 2500.0 ? 1 : 0;
        TEST_CHECK(r.floor >= field.params().geography.landMin);
    }
    TEST_CHECK(deep * 10 >= static_cast<int>(rifts.size()) * 8);
    const terrain::RiftProfile& largest = rifts.front();
    const std::size_t center = largest.heights.size() / 2;
    double floorSlope = 0.0;
    double wallSlope = 0.0;
    for (std::size_t k = 1; k < largest.heights.size(); ++k) {
        const double slope =
            std::abs(static_cast<double>(largest.heights[k] - largest.heights[k - 1])) /
            terrain::kRiftStep;
        const auto offset = static_cast<double>(k) - static_cast<double>(center);
        if (std::abs(offset) * terrain::kRiftStep <= 5'000.0) {
            floorSlope = std::max(floorSlope, slope);
        }
        wallSlope = std::max(wallSlope, slope);
    }
    std::printf("largest rift: floor slope %.3f, wall slope %.3f\n", floorSlope, wallSlope);
    TEST_CHECK(floorSlope < 0.1);
    TEST_CHECK(wallSlope > 0.577);
}

} // namespace

int main() {
    const Stages stages;
    testBendDoesNotFold(stages);
    testDraws(stages);
    testRangesEndGradually(stages);
    testRifts();
    return test::result();
}
