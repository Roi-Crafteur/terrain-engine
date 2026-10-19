#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/rivers.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace {

using terrain::HeightGrid;
using terrain::kNoReceiver;
using terrain::kSeaLevel;
using terrain::RiverNetwork;
using terrain::RiverParams;

constexpr std::uint64_t kSeed = 1234;

HeightGrid makeGrid(int size, double metersPerPixel, const std::function<float(int, int)>& height) {
    HeightGrid grid{size, metersPerPixel, {}};
    for (int row = 0; row < size; ++row) {
        for (int column = 0; column < size; ++column) {
            grid.heights.push_back(height(column, row));
        }
    }
    return grid;
}

bool isBorder(const HeightGrid& grid, int index) {
    const int column = index % grid.size;
    const int row = index / grid.size;
    return column == 0 || row == 0 || column == grid.size - 1 || row == grid.size - 1;
}

bool isOcean(const HeightGrid& grid, int index) {
    return grid.heights[static_cast<std::size_t>(index)] < kSeaLevel;
}

float heightOf(const HeightGrid& grid, int index) {
    return grid.heights[static_cast<std::size_t>(index)];
}

// Heights of the 8 neighbors that exist.
std::vector<float> neighborHeights(const HeightGrid& grid, int index) {
    std::vector<float> heights;
    const int column = index % grid.size;
    const int row = index / grid.size;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int c = column + dx;
            const int r = row + dz;
            if ((dx != 0 || dz != 0) && c >= 0 && r >= 0 && c < grid.size && r < grid.size) {
                heights.push_back(grid.at(c, r));
            }
        }
    }
    return heights;
}

// Every land cell drains: it is an outlet or has a strictly lower neighbor.
bool everyLandCellDrains(const HeightGrid& grid) {
    bool drains = true;
    for (int i = 0; i < static_cast<int>(grid.heights.size()); ++i) {
        if (isOcean(grid, i) || isBorder(grid, i)) {
            continue;
        }
        const std::vector<float> around = neighborHeights(grid, i);
        drains = drains && *std::min_element(around.begin(), around.end()) < heightOf(grid, i);
    }
    return drains;
}

// River invariants on the carved grid: each river cell flows to a river cell or into the
// ocean, never uphill, and every river path ends at an outlet (ocean or border).
// Returns the number of river cells.
int checkRiversConnected(const HeightGrid& grid, const RiverNetwork& network) {
    int rivers = 0;
    bool connected = true;
    bool downhill = true;
    bool reachesOutlet = true;
    for (int i = 0; i < static_cast<int>(grid.heights.size()); ++i) {
        if (!network.isRiver(i)) {
            continue;
        }
        ++rivers;
        const int receiver = network.receiverOf(i);
        if (receiver != kNoReceiver) {
            connected = connected && (network.isRiver(receiver) || isOcean(grid, receiver));
            downhill = downhill && heightOf(grid, receiver) <= heightOf(grid, i);
        }
        int cell = i;
        int steps = 0;
        while (network.receiverOf(cell) != kNoReceiver && steps <= grid.size * grid.size) {
            cell = network.receiverOf(cell);
            ++steps;
        }
        reachesOutlet = reachesOutlet && (isOcean(grid, cell) || isBorder(grid, cell));
    }
    TEST_CHECK(connected);
    TEST_CHECK(downhill);
    TEST_CHECK(reachesOutlet);
    return rivers;
}

// Plane rising away from an ocean on row 0: 5 m per row, 10 m cells.
HeightGrid oceanPlane(float metersPerRow) {
    return makeGrid(8, 10.0, [=](int, int row) {
        return row == 0 ? -10.0f : metersPerRow * static_cast<float>(row);
    });
}

// ---------------------------------------------------------------------------

void testFillDepressions() {
    // Slope toward an ocean column, with a 4 x 4 bowl and a single-cell pit.
    const HeightGrid raw = makeGrid(16, 10.0, [](int column, int row) {
        const auto c = static_cast<float>(column);
        float h = column == 0 ? -10.0f : 50.0f + 5.0f * c + 0.5f * static_cast<float>(row);
        if (column >= 6 && column <= 9 && row >= 6 && row <= 9) {
            h -= 40.0f;
        }
        if (column == 12 && row == 3) {
            h -= 20.0f;
        }
        return h;
    });
    TEST_CHECK(!everyLandCellDrains(raw));
    HeightGrid filled = raw;
    terrain::fillDepressions(filled);
    TEST_CHECK(everyLandCellDrains(filled));

    bool onlyRaised = true;
    bool outletsKept = true;
    bool outsideKept = true;
    for (int i = 0; i < static_cast<int>(raw.heights.size()); ++i) {
        onlyRaised = onlyRaised && heightOf(filled, i) >= heightOf(raw, i);
        if (isOcean(raw, i) || isBorder(raw, i)) {
            outletsKept = outletsKept && heightOf(filled, i) == heightOf(raw, i);
        }
        if (i % raw.size <= 4) { // downstream of both depressions
            outsideKept = outsideKept && heightOf(filled, i) == heightOf(raw, i);
        }
    }
    TEST_CHECK(onlyRaised);
    TEST_CHECK(outletsKept);
    TEST_CHECK(outsideKept);
    TEST_CHECK(filled.at(12, 3) > raw.at(12, 3));
    TEST_CHECK(filled.at(7, 7) > raw.at(7, 7));
}

void testDrainageOnPlane() {
    const HeightGrid plane = oceanPlane(5.0f);
    RiverParams params;
    params.minCatchmentArea = 300.0; // 3 cells of 100 m^2
    const RiverNetwork network = terrain::computeDrainage(plane, kSeed, params);
    bool receivers = true;
    bool areas = true;
    bool rivers = true;
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const auto i = static_cast<int>(plane.index(column, row));
            const bool interior = column > 0 && column < 7 && row > 0 && row < 7;
            const int expected = interior ? static_cast<int>(plane.index(column, row - 1))
                                          : kNoReceiver;
            receivers = receivers && network.receiverOf(i) == expected;
            double area = 100.0; // the cell itself
            if (row == 0) {
                area = column > 0 && column < 7 ? 600.0 : 0.0; // ocean: inflow only
            } else if (interior) {
                area = 100.0 * (7 - row);
            }
            areas = areas && network.catchment[static_cast<std::size_t>(i)] == area;
            rivers = rivers && network.isRiver(i) == (row > 0 && area >= 300.0);
        }
    }
    TEST_CHECK(receivers);
    TEST_CHECK(areas);
    TEST_CHECK(rivers);

    // Every cell appears once in the drainage order, before its receiver.
    std::vector<int> position(plane.heights.size(), -1);
    for (int k = 0; k < static_cast<int>(network.downstreamOrder.size()); ++k) {
        const int cell = network.downstreamOrder[static_cast<std::size_t>(k)];
        position[static_cast<std::size_t>(cell)] = k;
    }
    bool ordered = network.downstreamOrder.size() == plane.heights.size();
    for (int i = 0; i < static_cast<int>(plane.heights.size()); ++i) {
        const int receiver = network.receiverOf(i);
        ordered = ordered && position[static_cast<std::size_t>(i)] >= 0 &&
                  (receiver == kNoReceiver || position[static_cast<std::size_t>(i)] <
                                                  position[static_cast<std::size_t>(receiver)]);
    }
    TEST_CHECK(ordered);
}

void testCarving() {
    RiverParams params;
    params.minCatchmentArea = 300.0;
    // Steep plane: every bed is exactly its cell height minus its depth.
    const HeightGrid steep = oceanPlane(5.0f);
    HeightGrid carved = steep;
    RiverNetwork network = terrain::computeDrainage(carved, kSeed, params);
    terrain::carveRiverBeds(carved, network, params);
    bool exact = true;
    for (int i = 0; i < static_cast<int>(steep.heights.size()); ++i) {
        const bool carvable = network.isRiver(i) && network.receiverOf(i) != kNoReceiver;
        const float depth =
            carvable ? terrain::bedDepth(network.catchment[static_cast<std::size_t>(i)], params)
                     : 0.0f;
        exact = exact && depth <= params.maxBedDepth &&
                std::abs(heightOf(carved, i) - (heightOf(steep, i) - depth)) <= 1.0e-4f;
    }
    TEST_CHECK(exact);
    TEST_CHECK(terrain::bedDepth(300.0, params) == params.bedDepth);
    TEST_CHECK(terrain::bedDepth(600.0, params) == params.bedDepth + params.bedDepthPerDoubling);
    TEST_CHECK(terrain::bedDepth(1.0e12, params) == params.maxBedDepth);
    checkRiversConnected(carved, network);

    // Nearly flat plane: beds would go below the sea, they stop at sea level instead and
    // never rise downstream.
    const HeightGrid flat = oceanPlane(0.5f);
    carved = flat;
    network = terrain::computeDrainage(carved, kSeed, params);
    terrain::carveRiverBeds(carved, network, params);
    bool aboveSea = true;
    for (int i = 0; i < static_cast<int>(flat.heights.size()); ++i) {
        aboveSea = aboveSea && (isOcean(flat, i) || heightOf(carved, i) >= kSeaLevel);
    }
    TEST_CHECK(aboveSea);
    TEST_CHECK(carved.at(3, 1) == kSeaLevel);
    checkRiversConnected(carved, network);
}

// Hilly terrain sloping to an ocean, with closed depressions.
void testRiversConnected() {
    HeightGrid grid = makeGrid(64, 30.0, [](int column, int row) {
        const terrain::noise::FbmParams fbm{.octaves = 4, .frequency = 1.0 / 16.0};
        const float hills = 300.0f * terrain::noise::fbm2D(column, row, kSeed, fbm);
        return row < 4 ? -20.0f : 15.0f * static_cast<float>(row) + hills;
    });
    RiverParams params;
    params.minCatchmentArea = 30.0 * 900.0;
    const RiverNetwork network = terrain::generateRivers(grid, kSeed, params);
    TEST_CHECK(checkRiversConnected(grid, network) > 50);
    int mouths = 0;
    for (int i = 0; i < static_cast<int>(grid.heights.size()); ++i) {
        const int receiver = network.receiverOf(i);
        const bool mouth = network.isRiver(i) && receiver != kNoReceiver && isOcean(grid, receiver);
        mouths += mouth ? 1 : 0;
    }
    TEST_CHECK(mouths > 0);
}

// Valley along row 16 of 50 m cells, draining toward column 0 (the border) with the slope
// alongSlope, side slopes 0.5, and a step of stepHeight meters from column stepColumn on.
HeightGrid valley(float alongSlope, int stepColumn, float stepHeight) {
    constexpr double kCell = 50.0;
    return makeGrid(32, kCell, [=](int column, int row) {
        const auto cell = static_cast<float>(kCell);
        const float along = alongSlope * cell * static_cast<float>(column);
        const float side = 0.5f * cell * static_cast<float>(std::abs(row - 16));
        return 1.0f + along + side + (column >= stepColumn ? stepHeight : 0.0f);
    });
}

RiverParams cascadeParams() {
    RiverParams params;
    params.minCatchmentArea = 8.0 * 2500.0; // 8 cells
    params.bedDepth = 0.5f;
    params.bedDepthPerDoubling = 0.0f;
    params.maxBedDepth = 0.5f;
    return params;
}

void testCascadeAtSlopeBreak() {
    constexpr int kStep = 20;
    HeightGrid grid = valley(0.05f, kStep, 120.0f);
    const RiverNetwork network = terrain::generateRivers(grid, kSeed, cascadeParams());
    checkRiversConnected(grid, network);
    bool atStep = !network.cascades.empty();
    bool mainRiver = false;
    for (const terrain::Cascade& cascade : network.cascades) {
        atStep = atStep && cascade.column == kStep && cascade.slopeBelow > cascade.slopeAbove;
        mainRiver = mainRiver || cascade.row == 16;
    }
    TEST_CHECK(atStep);
    TEST_CHECK(mainRiver);
}

// A steep river without a break in slope has no cascade.
void testNoCascadeOnConstantSlope() {
    HeightGrid grid = valley(0.8f, 0, 0.0f);
    const RiverParams params = cascadeParams();
    const RiverNetwork network = terrain::generateRivers(grid, kSeed, params);
    TEST_CHECK(checkRiversConnected(grid, network) > 20);
    TEST_CHECK(params.cascadeMinSlope < 0.8f);
    TEST_CHECK(network.cascades.empty());
}

} // namespace

int main() {
    testFillDepressions();
    testDrainageOnPlane();
    testCarving();
    testRiversConnected();
    testCascadeAtSlopeBreak();
    testNoCascadeOnConstantSlope();
    return test::result();
}
