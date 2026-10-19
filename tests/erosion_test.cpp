#include "terrain/generation/erosion.hpp"
#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/heightfield.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>

namespace {

using terrain::ErosionParams;
using terrain::HeightGrid;
using terrain::kSeaLevel;

HeightGrid makeGrid(int size, double metersPerPixel, const std::function<float(int, int)>& height) {
    HeightGrid grid{size, metersPerPixel, {}};
    for (int row = 0; row < size; ++row) {
        for (int column = 0; column < size; ++column) {
            grid.heights.push_back(height(column, row));
        }
    }
    return grid;
}

// Highest point of a coarse search around the origin: the heart of a mountain range.
HeightGrid mountainGrid(int size, double metersPerPixel) {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed);
    terrain::HeightmapRegion region{.metersPerPixel = metersPerPixel, .size = size};
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
    return terrain::sampleHeightGrid(field, region);
}

// Island: a cone rising from the sea floor, its coast at radius cells from the center.
HeightGrid islandGrid(int size, double metersPerPixel, float radius) {
    const float center = static_cast<float>(size - 1) / 2.0f;
    return makeGrid(size, metersPerPixel, [=](int column, int row) {
        const float distance = std::hypot(static_cast<float>(column) - center,
                                          static_cast<float>(row) - center);
        return 80.0f * (radius - distance);
    });
}

// Largest height difference between 4-connected neighbors, over the cell size.
double maxSlope(const HeightGrid& grid) {
    double steepest = 0.0;
    for (int row = 0; row < grid.size; ++row) {
        for (int column = 0; column + 1 < grid.size; ++column) {
            steepest = std::max(steepest, std::abs(static_cast<double>(grid.at(column + 1, row)) -
                                                   grid.at(column, row)));
            steepest = std::max(steepest, std::abs(static_cast<double>(grid.at(row, column + 1)) -
                                                   grid.at(row, column)));
        }
    }
    return steepest / grid.metersPerPixel;
}

// ---------------------------------------------------------------------------

void testDeterminism() {
    const HeightGrid raw = mountainGrid(64, 60.0);
    HeightGrid a = raw;
    HeightGrid b = raw;
    HeightGrid other = raw;
    terrain::erodeHeightGrid(a, 42);
    terrain::erodeHeightGrid(b, 42);
    terrain::erodeHeightGrid(other, 43);
    TEST_CHECK(a.heights == b.heights);
    TEST_CHECK(a.heights != other.heights);
    TEST_CHECK(a.heights != raw.heights);
}

// Finite heights, within the range of the relief before erosion.
void testValidValues() {
    const HeightGrid raw = mountainGrid(64, 60.0);
    HeightGrid eroded = raw;
    terrain::erodeHeightGrid(eroded, 7);
    const auto [low, high] = std::minmax_element(raw.heights.begin(), raw.heights.end());
    bool valid = eroded.heights.size() == raw.heights.size();
    for (const float h : eroded.heights) {
        valid = valid && std::isfinite(h) && h >= *low && h <= *high;
    }
    TEST_CHECK(valid);
    // Erosion smooths the relief: no new step steeper than the steepest slope before.
    TEST_CHECK(maxSlope(eroded) <= maxSlope(raw));
}

// Ocean cells are untouched and land stays at or above sea level: the coast is preserved.
void testCoastlinePreserved() {
    const HeightGrid raw = islandGrid(64, 50.0, 20.0f);
    using Step = std::function<void(HeightGrid&)>;
    const Step steps[] = {
        [](HeightGrid& g) { terrain::erodeHydraulic(g, 5); },
        [](HeightGrid& g) { terrain::smoothSteepSlopes(g); },
        [](HeightGrid& g) { terrain::erodeHeightGrid(g, 5); },
    };
    for (const Step& step : steps) {
        HeightGrid eroded = raw;
        step(eroded);
        bool preserved = true;
        int land = 0;
        for (std::size_t i = 0; i < raw.heights.size(); ++i) {
            const bool ocean = raw.heights[i] < kSeaLevel;
            preserved = preserved && (ocean ? eroded.heights[i] == raw.heights[i]
                                            : eroded.heights[i] >= kSeaLevel);
            land += ocean ? 0 : 1;
        }
        TEST_CHECK(preserved);
        TEST_CHECK(land > 500);
        TEST_CHECK(eroded.heights != raw.heights);
    }
}

void testBorderUnchanged() {
    const HeightGrid raw = mountainGrid(48, 60.0);
    HeightGrid eroded = raw;
    terrain::erodeHeightGrid(eroded, 9);
    bool unchanged = true;
    for (int k = 0; k < raw.size; ++k) {
        for (const auto& [column, row] : {std::pair{k, 0}, std::pair{k, raw.size - 1},
                                         std::pair{0, k}, std::pair{raw.size - 1, k}}) {
            unchanged = unchanged && eroded.at(column, row) == raw.at(column, row);
        }
    }
    TEST_CHECK(unchanged);

    constexpr int kMargin = 8;
    TEST_CHECK(terrain::borderWeight(0, 20, 48, kMargin) == 0.0f);
    TEST_CHECK(terrain::borderWeight(20, 47, 48, kMargin) == 0.0f);
    TEST_CHECK(terrain::borderWeight(kMargin, 20, 48, kMargin) == 1.0f);
    TEST_CHECK(terrain::borderWeight(24, 24, 48, kMargin) == 1.0f);
    bool increasing = true;
    for (int d = 0; d < kMargin; ++d) {
        increasing = increasing && terrain::borderWeight(d, 24, 48, kMargin) <
                                       terrain::borderWeight(d + 1, 24, 48, kMargin);
    }
    TEST_CHECK(increasing);
}

void testFlatGroundUnchanged() {
    const HeightGrid flat = makeGrid(32, 20.0, [](int, int) { return 150.0f; });
    HeightGrid eroded = flat;
    terrain::erodeHeightGrid(eroded, 11);
    TEST_CHECK(eroded.heights == flat.heights);
}

// Slopes steeper than the talus are lowered, conserving the material; gentle slopes are
// left exactly as they are.
void testThermalSmoothing() {
    ErosionParams params;
    params.thermalIterations = 200;
    const HeightGrid cliff =
        makeGrid(24, 10.0, [](int column, int) { return column < 12 ? 400.0f : 100.0f; });
    HeightGrid smoothed = cliff;
    terrain::smoothSteepSlopes(smoothed, params);
    TEST_CHECK(maxSlope(cliff) > 25.0);
    TEST_CHECK(maxSlope(smoothed) < 0.25 * maxSlope(cliff));
    double before = 0.0;
    double after = 0.0;
    for (std::size_t i = 0; i < cliff.heights.size(); ++i) {
        before += cliff.heights[i];
        after += smoothed.heights[i];
    }
    TEST_CHECK(std::abs(after - before) < 1.0e-4 * before);

    const HeightGrid gentle = makeGrid(24, 10.0, [&](int column, int row) {
        return static_cast<float>(column * 10.0 * 0.5 * params.talusSlope + row * 2.0);
    });
    HeightGrid unchanged = gentle;
    terrain::smoothSteepSlopes(unchanged, params);
    TEST_CHECK(unchanged.heights == gentle.heights);
}

// On a cone, droplets take material from the slopes and drop it lower down.
void testErosionAndDeposition() {
    const float center = 31.5f;
    const HeightGrid cone = makeGrid(64, 20.0, [=](int column, int row) {
        const float distance = std::hypot(static_cast<float>(column) - center,
                                          static_cast<float>(row) - center);
        return std::max(10.0f, 600.0f - 25.0f * distance);
    });
    HeightGrid eroded = cone;
    terrain::erodeHydraulic(eroded, 3);
    double upperChange = 0.0;
    int upper = 0;
    int raised = 0;
    for (std::size_t i = 0; i < cone.heights.size(); ++i) {
        const float change = eroded.heights[i] - cone.heights[i];
        if (cone.heights[i] > 300.0f) {
            upperChange += change;
            ++upper;
        }
        raised += change > 0.0f ? 1 : 0;
    }
    TEST_CHECK(upper > 0 && upperChange / upper < -1.0);
    TEST_CHECK(raised > 0);
}

} // namespace

int main() {
    testDeterminism();
    testValidValues();
    testCoastlinePreserved();
    testBorderUnchanged();
    testFlatGroundUnchanged();
    testThermalSmoothing();
    testErosionAndDeposition();
    return test::result();
}
