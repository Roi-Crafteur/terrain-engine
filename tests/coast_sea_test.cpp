// The sea of the coasts (agent 33) on the default world: the inlets (fjords, rias, calanques,
// coves) and the stacks never cut the sea off the ocean.
// - Around inlets of every kind (Heightfield::inletsIn), on a grid over a window holding each
//   whole: every point the inlets made sea (the coast signal of the world below 0 where that of
//   the geography is not), a step of the grid from their shore at least, is reached from the sea
//   of the geography at the edge of the window through the sea of the world (8 neighbors);
//   every point of the sea of the geography reached so is reached through the sea of the world
//   too (no stack closes a strait).
// - There, the water and the ground: the ocean exactly where the coast signal of the world is
//   below 0, the ground at most at the sea level there and at least at it elsewhere.
// - The inlets keep out of the inner part of the band (their sea where the coast signal of the
//   geography is below kInner).
#include "terrain/generation/coast.hpp"
#include "terrain/generation/heightfield.hpp"

#include "coast_zone_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <deque>
#include <vector>

namespace {

using terrain::CoastSite;
using terrain::Heightfield;
using terrain::InletKind;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
// A window holds an inlet whole: kGrid points per side at most, kOpening points across its mouth
// at least, kMostGrid points per side at most.
constexpr int kGrid = 140;
constexpr double kOpening = 5.0;
constexpr int kMostGrid = 400;
constexpr double kInner = 20'000.0;
// The coast signal per meter in the inlets (coast_inlets::kSignalPerMeter): a point more than a
// step of the grid inside their water lies in water wider than two steps, which the grid joins.
constexpr double kSignalPerMeter = 3.0;

const Heightfield& field() {
    static const Heightfield f(kSeed);
    return f;
}

// Coasts of each kind (found by the tests of the stop points), searched 100 km around.
constexpr std::array<std::array<double, 2>, 5> kCoasts{{{3'624'471.0, -10'006'796.0},
                                                        {1'793'548.0, 9'821'452.0},
                                                        {23'433'101.0, -2'128'828.0},
                                                        {-15'720'496.0, -6'266'783.0},
                                                        {-26'486'937.0, 7'219'391.0}}};

// kPerKind inlets of each kind near those coasts (the first ones by x, then z).
std::vector<CoastSite> inletsToCheck() {
    constexpr int kPerKind = 3;
    std::array<int, terrain::kInletKindCount> taken{};
    std::vector<CoastSite> out;
    for (const auto& [x, z] : kCoasts) {
        for (const CoastSite& s : field().inletsIn(x - 1e5, z - 1e5, x + 1e5, z + 1e5)) {
            int& n = taken[static_cast<std::size_t>(s.inlet)];
            if (n < kPerKind) {
                ++n;
                out.push_back(s);
            }
        }
    }
    for (const int n : taken) {
        TEST_CHECK(n == kPerKind);
    }
    return out;
}

struct Window {
    int n = kGrid;
    double x0 = 0.0;
    double z0 = 0.0;
    double step = 0.0;
    std::vector<char> base; // sea of the geography
    std::vector<char> sea;  // sea of the world
    std::vector<char> deep; // sea of the world a step from its shore at least
};

double sideOf(const CoastSite& s) {
    return 2.0 * static_cast<double>(s.length + s.size) + 800.0;
}

Window windowOf(const CoastSite& s) {
    const double side = sideOf(s);
    const double step =
        std::min(side / (kGrid - 1), static_cast<double>(s.opening) / kOpening);
    const int n = std::min(kMostGrid, static_cast<int>(side / step) + 1);
    Window w{n, s.x - 0.5 * side, s.z - 0.5 * side, side / (n - 1), {}, {}, {}};
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const terrain::CoastSample c = field().coastAt(w.x0 + i * w.step, w.z0 + j * w.step);
            w.base.push_back(c.base < 0.0 ? 1 : 0);
            w.sea.push_back(c.signal < 0.0 ? 1 : 0);
            w.deep.push_back(c.signal < -kSignalPerMeter * w.step ? 1 : 0);
        }
    }
    return w;
}

// The points of a mask reached from its points on the edge of the window that are also in the
// sea of the geography, through the mask.
std::vector<char> reached(const Window& w, const std::vector<char>& mask) {
    const int g = w.n;
    std::vector<char> seen(mask.size(), 0);
    std::deque<int> todo;
    for (int k = 0; k < g * g; ++k) {
        const int i = k % g;
        const int j = k / g;
        const bool edge = i == 0 || j == 0 || i == g - 1 || j == g - 1;
        if (edge && mask[k] != 0 && w.base[k] != 0) {
            seen[k] = 1;
            todo.push_back(k);
        }
    }
    while (!todo.empty()) {
        const int k = todo.front();
        todo.pop_front();
        const int i = k % g;
        const int j = k / g;
        for (const auto& [di, dj] : {std::array<int, 2>{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                     {1, 1}, {1, -1}, {-1, 1}, {-1, -1}}) {
            const int ni = i + di;
            const int nj = j + dj;
            const int n = nj * g + ni;
            if (ni >= 0 && nj >= 0 && ni < g && nj < g && mask[n] != 0 && seen[n] == 0) {
                seen[n] = 1;
                todo.push_back(n);
            }
        }
    }
    return seen;
}

void testConnected() {
    int inletPoints = 0;
    int cut = 0;
    int closed = 0;
    int inner = 0;
    const std::vector<CoastSite> sites = inletsToCheck();
    for (const CoastSite& s : sites) {
        const Window w = windowOf(s);
        const std::vector<char> world = reached(w, w.sea);
        const std::vector<char> geography = reached(w, w.base);
        for (int k = 0; k < w.n * w.n; ++k) {
            const bool made = w.sea[k] != 0 && w.base[k] == 0;
            inletPoints += made ? 1 : 0;
            cut += made && w.deep[k] != 0 && world[k] == 0 ? 1 : 0;
            closed += geography[k] != 0 && w.sea[k] != 0 && world[k] == 0 ? 1 : 0;
            const double x = w.x0 + (k % w.n) * w.step;
            const double z = w.z0 + (k / w.n) * w.step;
            inner += made && coast_zone::coastSignal(field(), x, z) >= kInner ? 1 : 0;
        }
    }
    std::printf("%zu inlets: %d points of their sea, %d cut off the ocean, %d points of the sea "
                "closed by the stacks, %d in the inner part of the band\n",
                sites.size(), inletPoints, cut, closed, inner);
    TEST_CHECK(inletPoints > 0);
    TEST_CHECK(cut == 0);
    TEST_CHECK(closed == 0);
    TEST_CHECK(inner == 0);
}

// The ocean exactly where the coast signal of the world is below 0; the ground at most at the sea
// level there, at least at it elsewhere: on every fourth point of the windows of three inlets.
void testGround() {
    int points = 0;
    int wrong = 0;
    const std::vector<CoastSite> sites = inletsToCheck();
    for (std::size_t n = 0; n < sites.size(); n += 4) {
        const double side = sideOf(sites[n]);
        for (int j = 0; j < kGrid; j += 4) {
            for (int i = 0; i < kGrid; i += 4) {
                const double x = sites[n].x + side * (static_cast<double>(i) / (kGrid - 1) - 0.5);
                const double z = sites[n].z + side * (static_cast<double>(j) / (kGrid - 1) - 0.5);
                const bool sea = field().coastAt(x, z).signal < 0.0;
                const terrain::HeightSample h = field().sampleAt(x, z);
                const bool ocean = h.water == terrain::WaterKind::Ocean;
                wrong += ocean != sea || (sea ? h.height > 0.0f : h.height < 0.0f) ? 1 : 0;
                ++points;
            }
        }
    }
    std::printf("%d points: %d with the ocean or the ground on the wrong side of the shore\n",
                points, wrong);
    TEST_CHECK(wrong == 0);
}

} // namespace

int main() {
    testConnected();
    testGround();
    return test::result();
}
