// Figures of a relief on synthetic grids whose answers are known: an island, a rectangular
// chain on a plateau, a straight and a wavy coast, the altitude bands; the divider lengths of
// known lines.
#include "terrain/io/relief_stats.hpp"

#include "test_assert.hpp"

#include <cmath>
#include <cstddef>
#include <functional>
#include <numbers>
#include <stdexcept>

namespace {

using terrain::ReliefGrid;
using terrain::ReliefStats;

constexpr double kSpacing = 10'000.0;

// A wrapping grid of columns x rows cells of kSpacing, height(i, j) at each cell.
ReliefGrid grid(int columns, int rows, const std::function<float(int, int)>& height) {
    ReliefGrid g;
    g.columns = columns;
    g.rows = rows;
    g.spacing = kSpacing;
    g.originX = -0.5 * columns * kSpacing;
    g.originZ = -0.5 * rows * kSpacing;
    g.wraps = true;
    for (int j = 0; j < rows; ++j) {
        for (int i = 0; i < columns; ++i) {
            g.heights.push_back(height(i, j));
        }
    }
    return g;
}

bool near(double value, double expected, double tolerance) {
    return std::abs(value - expected) <= tolerance * std::abs(expected);
}

void testIsland() {
    const double radius = 12.0;
    const ReliefGrid g = grid(96, 64, [radius](int i, int j) {
        return std::hypot(i - 48.0, j - 32.0) <= radius ? 300.0f : -2000.0f;
    });
    terrain::ReliefStatsParams params;
    params.continentArea = 1.0e10; // 10,000 km2
    const ReliefStats s = terrain::measureRelief(g, params);
    const double area = std::numbers::pi * radius * radius * kSpacing * kSpacing;
    TEST_CHECK(s.landMasses.size() == 1);
    TEST_CHECK(near(s.landMasses.front().area, area, 0.1));
    TEST_CHECK(near(s.landShare, area / (96.0 * 64.0 * kSpacing * kSpacing), 0.1));
    TEST_CHECK(s.continents == 1);
    TEST_CHECK(s.chains.empty() && s.mountainShare == 0.0);
    TEST_CHECK(s.altitudeShares[1] == 1.0); // 300 m: the band from 200 to 500 m
    TEST_CHECK(s.highest == 300.0f);
    TEST_CHECK(near(s.coastLength, 2.0 * std::numbers::pi * radius * kSpacing, 0.1));
    // No plain site: the island is smaller than 1,000,000 km2.
    TEST_CHECK(s.coast.found && !s.plain.found);
    TEST_CHECK(!s.mountain.found);
}

// A plateau at 500 m with a chain 60 cells long and 4 wide, rough (2,000 and 3,000 m).
void testChain() {
    const ReliefGrid g = grid(128, 64, [](int i, int j) {
        if (i < 20 || i >= 110 || j < 12 || j >= 52) {
            return -1000.0f;
        }
        if (i >= 35 && i < 95 && j >= 30 && j < 34) {
            return (i + j) % 2 == 0 ? 3000.0f : 2000.0f;
        }
        return 500.0f;
    });
    const ReliefStats s = terrain::measureRelief(g);
    TEST_CHECK(s.chains.size() == 1);
    if (!s.chains.empty()) {
        const terrain::MountainChain& c = s.chains.front();
        TEST_CHECK(near(c.length, 60.0 * kSpacing, 0.1));
        TEST_CHECK(near(c.width, 4.0 * kSpacing, 0.25));
        TEST_CHECK(c.highest == 3000.0f);
    }
    TEST_CHECK(s.mountain.found);
    TEST_CHECK(s.altitudeShares[2] > 0.0 && s.altitudeShares[4] > 0.0); // 500 m, 2,000 m
    // The ends of the plateau lie over 150 km from the chain, not 500 km.
    terrain::ReliefStatsParams near150;
    near150.plainDistance = 150'000.0;
    TEST_CHECK(terrain::measureRelief(g, near150).plainShare > 0.0);
    TEST_CHECK(s.plainShare == 0.0);
}

// A band of land between two straight north-south lines: two straight coasts across the whole
// world, which a wave of 6 cells every 16 cells breaks into short stretches.
void testStraightCoasts() {
    const ReliefGrid straight =
        grid(64, 96, [](int i, int) { return i > 16 && i < 48 ? 100.0f : -100.0f; });
    const ReliefStats s = terrain::measureRelief(straight);
    TEST_CHECK(s.longestStraightCoast.length > 90.0 * kSpacing);
    TEST_CHECK(s.longestStraightMapCoast.length > 90.0 * kSpacing);
    TEST_CHECK(near(s.coastLength, 2.0 * 96.0 * kSpacing, 0.02));
    const ReliefGrid wavy = grid(64, 96, [](int i, int j) {
        const double wave = 6.0 * std::sin(2.0 * std::numbers::pi * j / 16.0);
        return i > 16.0 + wave && i < 48.0 + wave ? 100.0f : -100.0f;
    });
    const ReliefStats w = terrain::measureRelief(wavy);
    // At most a flank of a wave, from a trough to a crest: about 140 km.
    TEST_CHECK(w.longestStraightCoast.length < 20.0 * kSpacing);
    TEST_CHECK(w.longestStraightCoast.length > 2.0 * kSpacing);
}

void testDividers() {
    terrain::relief_shapes::Line line;
    line.x = {0.0, 100.0};
    line.z = {0.0, 0.0};
    TEST_CHECK(std::abs(terrain::relief_shapes::dividerLength(line, 10.0) - 100.0) < 1e-9);
    TEST_CHECK(std::abs(terrain::relief_shapes::dividerLength(line, 30.0) - 100.0) < 1e-9);
    // A zigzag of 10 teeth 10 wide and 5 high: 1 m dividers follow it, 20 m ones cut across.
    terrain::relief_shapes::Line zigzag;
    for (int k = 0; k <= 20; ++k) {
        zigzag.x.push_back(5.0 * k);
        zigzag.z.push_back(k % 2 == 0 ? 0.0 : 5.0);
    }
    const double fine = terrain::relief_shapes::dividerLength(zigzag, 1.0);
    const double coarse = terrain::relief_shapes::dividerLength(zigzag, 20.0);
    TEST_CHECK(near(fine, terrain::relief_shapes::lineLength(zigzag), 0.05));
    TEST_CHECK(fine / coarse > 1.3);
}

void testRejectsWindows() {
    ReliefGrid g = grid(16, 16, [](int, int) { return 0.0f; });
    g.wraps = false;
    bool thrown = false;
    try {
        (void)terrain::measureRelief(g);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    TEST_CHECK(thrown);
}

} // namespace

int main() {
    testIsland();
    testChain();
    testStraightCoasts();
    testDividers();
    testRejectsWindows();
    return test::result();
}
