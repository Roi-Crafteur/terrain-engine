// The grid of the planet level: geometry on the torus, interpolation, relief sampling (the same
// bits as Heightfield::heightAt, whatever the number of threads) and the ocean mask.
#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_params.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include "test_assert.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

using terrain::PlanetGrid;
using terrain::WorldSize;

// 2,048 x 1,024 km: small enough for the tests.
const WorldSize kSmallWorld{32.0 * 65536.0, 16.0 * 65536.0};

bool throwsInvalid(const WorldSize& world, double cellSize) {
    try {
        (void)terrain::makePlanetGrid(world, cellSize);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

bool sameBits(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t c = 0; c < a.size(); ++c) {
        if (std::bit_cast<std::uint32_t>(a[c]) != std::bit_cast<std::uint32_t>(b[c])) {
            return false;
        }
    }
    return true;
}

void testMakeGrid() {
    const PlanetGrid grid = terrain::makePlanetGrid(WorldSize{}, 8192.0);
    TEST_CHECK(grid.nx == 7328 && grid.nz == 4272 && grid.cells() == 31305216u);
    TEST_CHECK(grid.periodX() == WorldSize{}.x && grid.periodZ() == WorldSize{}.z);
    TEST_CHECK(throwsInvalid(WorldSize{}, 10000.0)); // does not divide the sides
    TEST_CHECK(throwsInvalid(WorldSize{}, 0.0));
    TEST_CHECK(throwsInvalid(WorldSize{}, std::numeric_limits<double>::quiet_NaN()));
    TEST_CHECK(throwsInvalid({65536.0, 65536.0}, 32768.0));     // 2 cells per side
    TEST_CHECK(throwsInvalid({60'000'000.0, 65536.0}, 8192.0)); // invalid world
    TEST_CHECK(!throwsInvalid(kSmallWorld, 65536.0));
}

void testGeometry() {
    const PlanetGrid grid{8, 6, 1000.0};
    TEST_CHECK(grid.centerX(0) == -3500.0 && grid.centerX(7) == 3500.0);
    TEST_CHECK(grid.centerZ(0) == -2500.0 && grid.centerZ(5) == 2500.0);
    TEST_CHECK(grid.index(3, 2) == 19u && grid.column(19) == 3 && grid.row(19) == 2);
    // The corner cell has its neighbors across both seams, the polar line included.
    const std::size_t corner = grid.index(0, 0);
    TEST_CHECK(grid.neighbor(corner, 0) == grid.index(1, 0));
    TEST_CHECK(grid.neighbor(corner, 1) == grid.index(7, 0));
    TEST_CHECK(grid.neighbor(corner, 2) == grid.index(0, 1));
    TEST_CHECK(grid.neighbor(corner, 3) == grid.index(0, 5));
    TEST_CHECK(grid.neighbor(corner, 7) == grid.index(7, 5));
    const std::size_t far = grid.index(7, 5);
    TEST_CHECK(grid.neighbor(far, 4) == grid.index(0, 0));
    // Each cell has 8 distinct neighbors, none of them itself.
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        for (int a = 0; a < 8; ++a) {
            TEST_CHECK(grid.neighbor(c, a) != c);
            for (int b = a + 1; b < 8; ++b) {
                TEST_CHECK(grid.neighbor(c, a) != grid.neighbor(c, b));
            }
        }
    }
}

void testBilinear() {
    const PlanetGrid grid{8, 6, 1000.0};
    // Exact at a center.
    const terrain::PlanetBilinear center = terrain::planetBilinear(grid, -1500.0, 500.0);
    TEST_CHECK(center.cells[0] == grid.index(2, 3) && center.tx == 0.0 && center.tz == 0.0);
    TEST_CHECK(center.blend(5.0, 7.0, 11.0, 13.0) == 5.0);
    // Across the east-west seam: halfway between the last and the first column.
    const terrain::PlanetBilinear seam = terrain::planetBilinear(grid, 4000.0, -2500.0);
    TEST_CHECK(seam.cells[0] == grid.index(7, 0) && seam.cells[1] == grid.index(0, 0));
    TEST_CHECK(seam.tx == 0.5 && seam.tz == 0.0);
    // Across the polar line, and every image of a point gives the same weights.
    const terrain::PlanetBilinear pole = terrain::planetBilinear(grid, -250.0, 2750.0);
    TEST_CHECK(pole.cells[0] == grid.index(3, 5) && pole.cells[2] == grid.index(3, 0));
    TEST_CHECK(pole.tx == 0.25 && pole.tz == 0.25);
    const terrain::PlanetBilinear image = terrain::planetBilinear(grid, -250.0 + 8000.0, -3250.0);
    TEST_CHECK(image.cells == pole.cells && image.tx == pole.tx && image.tz == pole.tz);
    TEST_CHECK(terrain::planetCellAt(grid, 3999.0, -2999.0) == grid.index(7, 0));
    TEST_CHECK(terrain::planetCellAt(grid, 4001.0 + 16000.0, 3001.0) == grid.index(0, 0));
}

void testSampleRelief() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed, {}, kSmallWorld);
    const PlanetGrid grid = terrain::makePlanetGrid(kSmallWorld, 16384.0);
    const std::vector<float> one = terrain::planet::sampleRelief(field, grid, 1, 1);
    bool exact = true;
    for (int j = 0; j < grid.nz; ++j) {
        for (int i = 0; i < grid.nx; ++i) {
            exact = exact && std::bit_cast<std::uint32_t>(one[grid.index(i, j)]) ==
                                 std::bit_cast<std::uint32_t>(
                                     field.heightAt(grid.centerX(i), grid.centerZ(j)));
        }
    }
    TEST_CHECK(exact);
    TEST_CHECK(sameBits(one, terrain::planet::sampleRelief(field, grid, 1, 3)));
    // 2 x 2: the mean of the centers of the 4 quarters.
    const std::vector<float> four = terrain::planet::sampleRelief(field, grid, 2, 1);
    const double q = 0.25 * grid.cellSize;
    const double x = grid.centerX(5);
    const double z = grid.centerZ(7);
    const double mean =
        (static_cast<double>(field.heightAt(x - q, z - q)) + field.heightAt(x + q, z - q) +
         field.heightAt(x - q, z + q) + field.heightAt(x + q, z + q)) /
        4.0;
    TEST_CHECK(four[grid.index(5, 7)] == static_cast<float>(mean));
    TEST_CHECK(sameBits(four, terrain::planet::sampleRelief(field, grid, 2, 4)));
    bool rejected = false;
    try {
        (void)terrain::planet::sampleRelief(field, grid, 0, 1);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    TEST_CHECK(rejected);
    rejected = false;
    try {
        (void)terrain::planet::sampleRelief(field, terrain::makePlanetGrid(WorldSize{}, 65536.0), 1,
                                            1);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    TEST_CHECK(rejected);
}

void testOcean() {
    using terrain::planet::kForcedOutlet;
    using terrain::planet::kLand;
    using terrain::planet::kOcean;
    const PlanetGrid grid{8, 6, 1000.0};
    std::vector<float> height(grid.cells(), 100.0f);
    // A sea of 6 cells across the east-west seam, and a pit of 2 cells.
    for (int i : {6, 7, 0}) {
        height[grid.index(i, 1)] = -50.0f;
        height[grid.index(i, 2)] = -80.0f;
    }
    height[grid.index(3, 4)] = -10.0f;
    height[grid.index(4, 4)] = -20.0f;
    const std::vector<std::uint8_t> ocean = terrain::planet::markOcean(grid, height, 5.0e6);
    TEST_CHECK(ocean[grid.index(7, 1)] == kOcean && ocean[grid.index(0, 2)] == kOcean);
    TEST_CHECK(ocean[grid.index(3, 4)] == kLand && ocean[grid.index(4, 4)] == kLand);
    TEST_CHECK(ocean[grid.index(2, 2)] == kLand);
    // Too small to be an ocean: the lowest cell becomes the outlet of the world.
    const std::vector<std::uint8_t> none = terrain::planet::markOcean(grid, height, 1.0e7);
    std::size_t outlets = 0;
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        outlets += none[c] == kForcedOutlet ? 1u : 0u;
        TEST_CHECK(none[c] != kOcean);
    }
    TEST_CHECK(outlets == 1 && none[grid.index(0, 2)] == kForcedOutlet); // lowest index of 3 ties
    // Ties are broken by the index, and -0 equals +0.
    const std::vector<float> flat(grid.cells(), 0.0f);
    TEST_CHECK(terrain::planet::markOcean(grid, flat, 1.0)[0] == kForcedOutlet);
    const std::vector<float> zeros{-0.0f, 0.0f};
    TEST_CHECK(terrain::planet::lowerCell(zeros, 0, 1) && !terrain::planet::lowerCell(zeros, 1, 0));
}

void testPresets() {
    const terrain::HeightfieldParams planetary = terrain::planetaryHeightfieldParams();
    TEST_CHECK(planetary.plates.amplitude == 0.5f && planetary.macroContinent == 0.25f);
    const terrain::HeightfieldParams defaults;
    TEST_CHECK(defaults.plates.amplitude == 0.0f && defaults.macroContinent == 0.0f);
    TEST_CHECK(terrain::PlanetParams{}.cellSize == 8192.0);
}

} // namespace

int main() {
    testMakeGrid();
    testGeometry();
    testBilinear();
    testSampleRelief();
    testOcean();
    testPresets();
    return test::result();
}
