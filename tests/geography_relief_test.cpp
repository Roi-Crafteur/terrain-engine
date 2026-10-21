// The relief of the planetary geography (phase 11R): the tree of the drainage of the map leads
// every land node to the sea down strictly lower nodes, draining ever larger areas; the valleys
// it carves leave shares in [0, 1], steeper among mountains than among hills; the hills and the
// mountains drain at the scale of the cells of the planet level (8 km): few pits on an 8 km grid
// in a belt and among hills, found by search; the plains are calm.
#include "terrain/generation/geography.hpp"
#include "terrain/generation/heightfield.hpp"

#include "bit_exact_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace {

using terrain::Geography;
using terrain::Heightfield;
using terrain::HeightfieldParams;
using terrain::WorldSize;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

const Heightfield& field() {
    static const Heightfield f = [] {
        HeightfieldParams params;
        params.geography.enabled = true;
        return Heightfield(kSeed, params);
    }();
    return f;
}

// The point of the world at node (i, j) of the map.
struct Place {
    double x;
    double z;
};

Place placeOf(const Geography& g, int i, int j) {
    return {terrain::wrapCoordinate(-0.5 * g.world().x + i * g.world().x / g.columns() -
                                        g.anchorX(),
                                    g.world().x),
            -0.5 * g.world().z + j * g.world().z / g.rows()};
}

// From every land node, the steps downstream reach the sea, each to a strictly lower node
// draining a strictly larger area.
void testDrainageTree() {
    const Geography& g = *field().geography();
    bool reaches = true;
    bool descends = true;
    for (int j = 0; j < g.rows(); ++j) {
        for (int i = 0; i < g.columns(); ++i) {
            if (g.node(i, j, Geography::kCoast) <= 0.0f) {
                continue;
            }
            int a = i;
            int b = j;
            int steps = 0;
            int di = 0;
            int dj = 0;
            while (g.node(a, b, Geography::kCoast) > 0.0f && steps < g.columns() * g.rows()) {
                if (!g.downstream(a, b, di, dj)) {
                    reaches = false;
                    break;
                }
                const bool land = g.node(a + di, b + dj, Geography::kCoast) > 0.0f;
                descends = descends && (!land || (g.node(a + di, b + dj, Geography::kLand) <
                                                      g.node(a, b, Geography::kLand) &&
                                                  g.drainedArea(a + di, b + dj) >
                                                      g.drainedArea(a, b)));
                a += di;
                b += dj;
                ++steps;
            }
            reaches = reaches && g.node(a, b, Geography::kCoast) <= 0.0f;
            if (!reaches || !descends) {
                std::printf("node (%d, %d) does not drain to the sea\n", i, j);
                TEST_CHECK(false);
                return;
            }
        }
    }
    int di = 0;
    int dj = 0;
    TEST_CHECK(!g.downstream(0, 0, di, dj) || g.node(0, 0, Geography::kCoast) > 0.0f);
}

void testValleyShapes() {
    const Geography& g = *field().geography();
    int land = 0;
    int floors = 0;
    bool bounded = true;
    for (const bit_exact::Point& p : bit_exact::randomPoints(20'000, 3e7)) {
        const double x = terrain::wrapCoordinate(p.x, g.world().x);
        const double z = terrain::wrapCoordinate(p.z, g.world().z);
        if (g.sampleAt(x, z).coast <= 0.0) {
            continue;
        }
        const terrain::ValleyShape v = g.valleyShape(x, z);
        bounded = bounded && v.hills >= 0.0f && v.hills <= 1.0f && v.mountains >= v.hills &&
                  v.mountains <= 1.0f;
        ++land;
        floors += v.hills == 0.0f ? 1 : 0;
    }
    TEST_CHECK(bounded);
    TEST_CHECK(floors > land / 50 && floors < land * 6 / 10);
}

// The first node of the map whose fields pass the test, far from the coast.
bool findNode(const Geography& g, const std::function<bool(int, int)>& wanted, int& ni, int& nj) {
    for (int j = 0; j < g.rows(); ++j) {
        for (int i = 0; i < g.columns(); ++i) {
            if (g.node(i, j, Geography::kCoast) > 400'000.0f && wanted(i, j)) {
                ni = i;
                nj = j;
                return true;
            }
        }
    }
    return false;
}

// Share of the cells of a 32 x 32 grid of 8 km around a place lower than their 8 neighbors.
double pitShare(const Place& c) {
    constexpr int kSide = 32;
    constexpr double kCell = 8192.0;
    std::vector<float> h(kSide * kSide);
    for (int j = 0; j < kSide; ++j) {
        for (int i = 0; i < kSide; ++i) {
            h[static_cast<std::size_t>(j * kSide + i)] =
                field().heightAt(c.x + (i - kSide / 2) * kCell, c.z + (j - kSide / 2) * kCell);
        }
    }
    int pits = 0;
    for (int j = 1; j + 1 < kSide; ++j) {
        for (int i = 1; i + 1 < kSide; ++i) {
            bool lowest = true;
            for (int dj = -1; dj <= 1; ++dj) {
                for (int di = -1; di <= 1; ++di) {
                    lowest = lowest && ((di == 0 && dj == 0) ||
                                        h[static_cast<std::size_t>((j + dj) * kSide + i + di)] >
                                            h[static_cast<std::size_t>(j * kSide + i)]);
                }
            }
            pits += lowest ? 1 : 0;
        }
    }
    return static_cast<double>(pits) / ((kSide - 2) * (kSide - 2));
}

void testReliefDrains() {
    const Geography& g = *field().geography();
    int i = 0;
    int j = 0;
    TEST_CHECK(findNode(
        g, [&g](int a, int b) { return g.node(a, b, Geography::kBelt) > 0.6f; }, i, j));
    const double belt = pitShare(placeOf(g, i, j));
    TEST_CHECK(findNode(
        g,
        [&g](int a, int b) {
            return g.node(a, b, Geography::kHills) > 100.0f &&
                   g.node(a, b, Geography::kBelt) == 0.0f &&
                   g.node(a, b, Geography::kMassif) == 0.0f;
        },
        i, j));
    const double hills = pitShare(placeOf(g, i, j));
    std::printf("pits on an 8 km grid: %.1f%% in a belt, %.1f%% among hills\n", 100.0 * belt,
                100.0 * hills);
    TEST_CHECK(belt < 0.05);
    TEST_CHECK(hills < 0.01);
}

// A lowland far from belts and massifs: relief under 50 m over 10 km.
void testPlainsCalm() {
    const Geography& g = *field().geography();
    int i = 0;
    int j = 0;
    TEST_CHECK(findNode(
        g,
        [&g](int a, int b) {
            return g.node(a, b, Geography::kHills) == 0.0f &&
                   g.node(a, b, Geography::kBelt) == 0.0f &&
                   g.node(a, b, Geography::kMassif) == 0.0f;
        },
        i, j));
    const Place c = placeOf(g, i, j);
    float low = 1e30f;
    float high = -1e30f;
    for (int b = 0; b <= 10; ++b) {
        for (int a = 0; a <= 10; ++a) {
            const float h = field().heightAt(c.x + 1000.0 * a, c.z + 1000.0 * b);
            low = std::min(low, h);
            high = std::max(high, h);
        }
    }
    TEST_CHECK(high - low < 50.0f);
}

} // namespace

int main() {
    testDrainageTree();
    testValleyShapes();
    testReliefDrains();
    testPlainsCalm();
    return test::result();
}
