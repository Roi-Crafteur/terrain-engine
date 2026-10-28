// Hydrology of the planet level, first half: the total order of the cells, the receivers and
// the depression hierarchy (pits, merges at the passes, attachment to the ocean, basin labels).
#include "terrain/generation/planet/planet_hierarchy.hpp"
#include "terrain/generation/planet/planet_order.hpp"

#include "planet_test_support.hpp"
#include "test_assert.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using planet_test::Terrain;
using terrain::planet::DepressionHierarchy;
using terrain::planet::kNoCell;
using terrain::planet::kOceanNode;

DepressionHierarchy hierarchyOf(const Terrain& t, std::vector<std::uint8_t>* receivers = nullptr) {
    const std::vector<std::uint32_t> order = terrain::planet::sortCells(t.height, 1);
    const auto receiver = terrain::planet::computeReceivers(t.grid, t.height, t.land, 1);
    if (receivers != nullptr) {
        *receivers = receiver.expand();
    }
    // The land alone in the order gives the same hierarchy.
    const DepressionHierarchy h =
        terrain::planet::buildHierarchy(t.grid, t.height, t.land, order, receiver);
    const DepressionHierarchy onLand = terrain::planet::buildHierarchy(
        t.grid, t.height, t.land, terrain::planet::sortLandCells(t.height, *t.land, 1), receiver);
    TEST_CHECK(onLand.nodes.size() == h.nodes.size() && onLand.label.values() == h.label.values() &&
               onLand.own.expand() == h.own.expand());
    return h;
}

void testSortCells() {
    // 300,000 cells on 10 levels: many ties, several blocks.
    Terrain t = planet_test::noisyTerrain(600, 500, 7, 0.0f, 10.0f, 0);
    for (float& h : t.height) {
        h = static_cast<float>(static_cast<int>(h));
    }
    t.height[5] = -0.0f;
    t.height[3] = 0.0f;
    const std::vector<std::uint32_t> one = terrain::planet::sortCells(t.height, 1);
    bool sorted = one.size() == t.height.size();
    std::vector<bool> seen(one.size(), false);
    for (std::size_t k = 0; k < one.size(); ++k) {
        seen[one[k]] = true;
        if (k > 0) {
            sorted = sorted && terrain::planet::lowerCell(t.height, one[k - 1], one[k]);
        }
    }
    bool permutation = true;
    for (const bool s : seen) {
        permutation = permutation && s;
    }
    TEST_CHECK(sorted && permutation);
    for (const int threads : {2, 3, 7}) {
        TEST_CHECK(terrain::planet::sortCells(t.height, threads) == one);
    }
}

void testReceivers() {
    Terrain t = planet_test::noisyTerrain(40, 30, 3, -300.0f, 700.0f, 2);
    t.markOcean();
    const std::vector<std::uint8_t> receiver =
        terrain::planet::computeReceivers(t.grid, t.height, t.land, 1).expand();
    TEST_CHECK(receiver == terrain::planet::computeReceivers(t.grid, t.height, t.land, 4).expand());
    for (std::size_t c = 0; c < t.grid.cells(); ++c) {
        if (t.ocean[c] != terrain::planet::kLand) {
            TEST_CHECK(receiver[c] == terrain::kNoNeighbor);
            continue;
        }
        double best = -1.0;
        int expected = terrain::kNoNeighbor;
        for (int k = 0; k < 8; ++k) {
            const std::size_t n = t.grid.neighbor(c, k);
            const double slope = (static_cast<double>(t.height[c]) - t.height[n]) /
                                 (terrain::kPlanetNeighbors[static_cast<std::size_t>(k)].diagonal
                                      ? terrain::kPlanetDiagonal
                                      : 1.0);
            if (terrain::planet::lowerCell(t.height, n, c) && slope > best) {
                best = slope;
                expected = k;
            }
        }
        TEST_CHECK(receiver[c] == expected);
    }
    // On a flat, the ties are broken by the index: the left neighbor comes first.
    Terrain flat = planet_test::flatTerrain(6, 6, 10.0f);
    flat.markOcean();
    const std::vector<std::uint8_t> flatReceiver =
        terrain::planet::computeReceivers(flat.grid, flat.height, flat.land, 1).expand();
    TEST_CHECK(flatReceiver[flat.cell(3, 3)] == 1);
    TEST_CHECK(flat.ocean[0] == terrain::planet::kForcedOutlet &&
               flatReceiver[0] == terrain::kNoNeighbor);
}

void testTwoBasins() {
    const Terrain t = planet_test::twoBasinIsland();
    const DepressionHierarchy h = hierarchyOf(t);
    TEST_CHECK(h.nodes.size() == 4); // ocean, two leaves, their merge
    const auto& b = h.nodes[1];      // pit at 5 m comes first in the order
    const auto& a = h.nodes[2];
    const auto& merged = h.nodes[3];
    TEST_CHECK(b.leaf() && b.pit == t.cell(7, 3) && a.leaf() && a.pit == t.cell(4, 3));
    // They meet at the ridge cell (6, 3), the first one touching both basins.
    TEST_CHECK(merged.children[0] == 1 && merged.children[1] == 2);
    TEST_CHECK(a.parent == 3 && b.parent == 3 && a.pass == t.cell(6, 3) && b.pass == t.cell(6, 3));
    TEST_CHECK(a.partner == t.cell(7, 3) && b.partner == t.cell(5, 3));
    TEST_CHECK(a.geolink == 1 && b.geolink == 2);
    // The merged basin spills to the sea at (2, 4), next to the sea cell (1, 3).
    TEST_CHECK(merged.parent == kOceanNode && merged.pass == t.cell(2, 4));
    TEST_CHECK(merged.partner == t.cell(1, 3) && merged.geolink == kOceanNode);
    TEST_CHECK(h.own[t.cell(4, 4)] == 2 && h.own[t.cell(5, 3)] == 2 && h.own[t.cell(5, 4)] == 3);
    TEST_CHECK(h.own[t.cell(2, 4)] == kOceanNode && h.own[t.cell(9, 5)] == kOceanNode);
    TEST_CHECK(h.label[t.cell(8, 4)] == 1 && h.label[t.cell(3, 4)] == 2);
    TEST_CHECK(h.label[t.cell(9, 5)] == kOceanNode && h.label[t.cell(0, 0)] == kOceanNode);
}

void testThreeWayMerge() {
    // Three single-cell pits (1, 2 and 3 m), pairwise apart but all next to a cell of 10 m, in
    // land of 100 m; the sea is row 0.
    Terrain t = planet_test::flatTerrain(9, 9, 100.0f);
    for (int i = 0; i < 9; ++i) {
        t.at(i, 0) = -100.0f;
    }
    t.at(3, 3) = 1.0f;
    t.at(5, 4) = 2.0f;
    t.at(3, 5) = 3.0f;
    t.at(4, 4) = 10.0f;
    t.markOcean();
    const DepressionHierarchy h = hierarchyOf(t);
    TEST_CHECK(h.nodes.size() == 6);
    // In the order of the neighbors of (4, 4): (5, 4), (3, 5), then (3, 3).
    TEST_CHECK(h.nodes[4].children[0] == 2 && h.nodes[4].children[1] == 3);
    TEST_CHECK(h.nodes[5].children[0] == 4 && h.nodes[5].children[1] == 1);
    TEST_CHECK(h.nodes[2].partner == t.cell(3, 5) && h.nodes[3].partner == t.cell(5, 4));
    TEST_CHECK(h.nodes[4].partner == t.cell(3, 3) && h.nodes[1].partner == t.cell(5, 4));
    TEST_CHECK(h.nodes[1].pass == t.cell(4, 4) && h.nodes[4].pass == t.cell(4, 4));
    TEST_CHECK(h.nodes[2].geolink == 3 && h.nodes[4].geolink == 1 && h.nodes[1].geolink == 2);
    // The first 100 m cell next to both the merged pits and the land joined to the sea.
    TEST_CHECK(h.nodes[5].parent == kOceanNode && h.nodes[5].pass == t.cell(2, 2));
}

void testAcrossSeams() {
    // A basin on the four corners of the grid, across both seams; the sea is row 4.
    Terrain t = planet_test::flatTerrain(8, 8, 50.0f);
    for (int i = 0; i < 8; ++i) {
        t.at(i, 4) = -100.0f;
    }
    t.at(0, 0) = 1.0f;
    t.at(7, 0) = 5.0f;
    t.at(0, 7) = 5.0f;
    t.at(7, 7) = 5.0f;
    t.markOcean();
    std::vector<std::uint8_t> receiver;
    const DepressionHierarchy h = hierarchyOf(t, &receiver);
    TEST_CHECK(h.nodes.size() == 2 && h.nodes[1].pit == t.cell(0, 0));
    TEST_CHECK(h.label[t.cell(7, 7)] == 1 && h.label[t.cell(7, 0)] == 1 &&
               h.label[t.cell(0, 7)] == 1);
    TEST_CHECK(t.grid.neighbor(t.cell(7, 7), receiver[t.cell(7, 7)]) == t.cell(0, 0));
    TEST_CHECK(h.nodes[1].parent == kOceanNode);
}

void testNoOcean() {
    Terrain t = planet_test::noisyTerrain(20, 16, 11, 100.0f, 900.0f, 1);
    t.markOcean(1.0e30);
    const DepressionHierarchy h = hierarchyOf(t);
    std::size_t outlets = 0;
    for (std::size_t c = 0; c < t.grid.cells(); ++c) {
        if (t.ocean[c] == terrain::planet::kForcedOutlet) {
            ++outlets;
            TEST_CHECK(h.own[c] == kOceanNode && h.label[c] == kOceanNode);
        }
    }
    TEST_CHECK(outlets == 1 && h.nodes.size() > 2);
}

// Invariants on a rough random relief: labels follow the receivers, children come first, every
// cell is flooded before the pass of its node.
void testInvariants() {
    Terrain t = planet_test::noisyTerrain(64, 48, 5, -400.0f, 1200.0f, 1);
    t.markOcean(3.0e6);
    std::vector<std::uint8_t> receiver;
    const DepressionHierarchy h = hierarchyOf(t, &receiver);
    TEST_CHECK(h.nodes.size() > 50);
    bool labels = true;
    bool flooded = true;
    for (std::size_t c = 0; c < t.grid.cells(); ++c) {
        std::size_t end = c;
        while (receiver[end] != terrain::kNoNeighbor) {
            end = t.grid.neighbor(end, receiver[end]);
        }
        const std::int32_t expected =
            t.ocean[end] != terrain::planet::kLand ? kOceanNode : h.own[end];
        labels = labels && h.label[c] == expected;
        const std::int32_t own = h.own[c];
        if (own != kOceanNode) {
            flooded = flooded && terrain::planet::lowerCell(
                                     t.height, c, h.nodes[static_cast<std::size_t>(own)].pass);
        }
    }
    TEST_CHECK(labels && flooded);
    for (std::size_t n = 1; n < h.nodes.size(); ++n) {
        const auto& node = h.nodes[n];
        TEST_CHECK(node.parent == kOceanNode || static_cast<std::size_t>(node.parent) > n);
        TEST_CHECK(node.pass != kNoCell && node.partner != kNoCell && node.geolink >= 0);
        TEST_CHECK(node.geolink == kOceanNode ||
                   h.nodes[static_cast<std::size_t>(node.geolink)].leaf());
        if (node.leaf()) {
            TEST_CHECK(h.own[node.pit] == static_cast<std::int32_t>(n) &&
                       receiver[node.pit] == terrain::kNoNeighbor);
        } else {
            for (const std::int32_t child : node.children) {
                TEST_CHECK(static_cast<std::size_t>(child) < n &&
                           h.nodes[static_cast<std::size_t>(child)].parent ==
                               static_cast<std::int32_t>(n));
            }
        }
    }
}

} // namespace

int main() {
    testSortCells();
    testReceivers();
    testTwoBasins();
    testThreeWayMerge();
    testAcrossSeams();
    testNoOcean();
    testInvariants();
    return test::result();
}
