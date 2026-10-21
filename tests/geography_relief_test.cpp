// The relief of the planetary geography (phases 11R and 11R bis): the tree of the drainage of the
// map leads every land node to the sea down strictly lower nodes, draining ever larger areas.
// Around the regional elevation the relief rises and falls: the plains are never flat (10 to
// 150 m over 10 km), the belts are steep (their relief over 5 km, slopes above 30 degrees), the
// land lies below and above its local mean alike. Pits on an 8 km grid (the cells of the planet
// level) are allowed, the water follows the relief (phase 11B): their share is only bounded so
// that the lakes of the planet level stay a few percent of the land.
#include "terrain/generation/geography.hpp"
#include "terrain/generation/heightfield.hpp"

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

// Bounds of the pits on an 8 km grid (see testPits): about 8 % in the belts and 11 % in the
// plains in 11R bis (one cell in 9 for a noise without slope).
constexpr double kMaxBeltPits = 0.15;
constexpr double kMaxPlainPits = 0.2;

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

// The first count land nodes passing the test, far from the coast, spread over the map: one per
// block of rows.
std::vector<Place> places(const std::function<bool(int, int)>& wanted, int count) {
    const Geography& g = *field().geography();
    std::vector<Place> out;
    for (int block = 0; block < count; ++block) {
        bool found = false;
        for (int j = block * g.rows() / count; j < (block + 1) * g.rows() / count && !found; ++j) {
            for (int i = 0; i < g.columns() && !found; ++i) {
                if (g.node(i, j, Geography::kCoast) > 400'000.0f && wanted(i, j)) {
                    out.push_back(placeOf(g, i, j));
                    found = true;
                }
            }
        }
    }
    return out;
}

bool lowland(int i, int j) {
    const Geography& g = *field().geography();
    return g.node(i, j, Geography::kHills) == 0.0f && g.node(i, j, Geography::kBelt) == 0.0f &&
           g.node(i, j, Geography::kMassif) == 0.0f;
}

bool belt(int i, int j) {
    return field().geography()->node(i, j, Geography::kBelt) > 0.6f;
}

// max - min of side x side samples step meters apart from a place toward +x and +z.
float reliefFrom(const Place& c, int side, double step) {
    float low = 1e30f;
    float high = -1e30f;
    for (int b = 0; b < side; ++b) {
        for (int a = 0; a < side; ++a) {
            const float h = field().heightAt(c.x + step * a, c.z + step * b);
            low = std::min(low, h);
            high = std::max(high, h);
        }
    }
    return high - low;
}

void testPits() {
    double beltPits = 0.0;
    double plainPits = 0.0;
    const std::vector<Place> belts = places(belt, 4);
    const std::vector<Place> plains = places(lowland, 4);
    for (const Place& p : belts) {
        beltPits += pitShare(p) / static_cast<double>(belts.size());
    }
    for (const Place& p : plains) {
        plainPits += pitShare(p) / static_cast<double>(plains.size());
    }
    std::printf("pits on an 8 km grid: %.1f%% in the belts, %.1f%% in the plains\n",
                100.0 * beltPits, 100.0 * plainPits);
    TEST_CHECK(belts.size() == 4 && plains.size() == 4);
    TEST_CHECK(beltPits < kMaxBeltPits);
    TEST_CHECK(plainPits < kMaxPlainPits);
}

// The plains are alive: 10 to 150 m of relief over 10 km at every lowland place.
void testPlainsLive() {
    const std::vector<Place> plains = places(lowland, 16);
    TEST_CHECK(plains.size() >= 12);
    float low = 1e30f;
    float high = 0.0f;
    for (const Place& p : plains) {
        const float r = reliefFrom(p, 11, 1000.0);
        low = std::min(low, r);
        high = std::max(high, r);
    }
    std::printf("plains: relief over 10 km from %.0f to %.0f m\n", static_cast<double>(low),
                static_cast<double>(high));
    TEST_CHECK(low >= 10.0f && high <= 150.0f);
}

// The belts are steep: their median relief over 5 km reaches 1,000 m and some of their slopes at
// 25 m pass 30 degrees.
void testMountainsSteep() {
    std::vector<float> reliefs;
    int steep = 0;
    int slopes = 0;
    for (const Place& p : places(belt, 16)) {
        reliefs.push_back(reliefFrom(p, 11, 500.0));
        for (int k = 0; k < 400; ++k) {
            const double x = p.x + 25.0 * (k % 20);
            const double z = p.z + 25.0 * (k / 20);
            const double gx = (field().heightAt(x + 25.0, z) - field().heightAt(x - 25.0, z)) / 50.0;
            const double gz = (field().heightAt(x, z + 25.0) - field().heightAt(x, z - 25.0)) / 50.0;
            steep += std::sqrt(gx * gx + gz * gz) > 0.57735 ? 1 : 0;
            ++slopes;
        }
    }
    std::sort(reliefs.begin(), reliefs.end());
    const float median = reliefs.empty() ? 0.0f : reliefs[reliefs.size() / 2];
    std::printf("belts: relief over 5 km %.0f m (median), slopes above 30 degrees %.1f%%\n",
                static_cast<double>(median), 100.0 * steep / std::max(slopes, 1));
    TEST_CHECK(reliefs.size() >= 12);
    TEST_CHECK(median >= 1000.0f);
    TEST_CHECK(steep > 0);
}

// The relief rises and falls around the regional elevation: about as much land below its mean
// within 1 km as above it.
void testRiseAndFall() {
    int below = 0;
    int points = 0;
    for (const Place& p : places([](int i, int j) { return lowland(i, j) || belt(i, j); }, 16)) {
        constexpr int kHalf = 10;
        constexpr double kStep = 100.0;
        for (int k = 0; k < 9; ++k) {
            const double x = p.x + 3000.0 * (k % 3);
            const double z = p.z + 3000.0 * (k / 3);
            double sum = 0.0;
            for (int b = -kHalf; b <= kHalf; ++b) {
                for (int a = -kHalf; a <= kHalf; ++a) {
                    sum += field().heightAt(x + kStep * a, z + kStep * b);
                }
            }
            below += field().heightAt(x, z) < sum / ((2 * kHalf + 1) * (2 * kHalf + 1)) ? 1 : 0;
            ++points;
        }
    }
    const double share = static_cast<double>(below) / std::max(points, 1);
    std::printf("land below its mean within 1 km: %.1f%% of %d points\n", 100.0 * share, points);
    TEST_CHECK(share > 0.3 && share < 0.7);
}

} // namespace

int main() {
    testDrainageTree();
    testPits();
    testPlainsLive();
    testMountainsSteep();
    testRiseAndFall();
    return test::result();
}
