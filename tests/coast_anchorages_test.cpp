// The anchorages of the coasts (agent 33, Heightfield::anchoragesIn) on the default world, beside
// the harbors (Heightfield::harborsIn):
// - anchorages found near coasts of rocky shores, cliffs, calanques and fjords; each a cove that
//   is no harbor, its mouth at least kHarborOpening wide, its floor at least kHarborDepth deep, at
//   least kAnchorageLength times as long as its mouth is wide; anchoragesIn exactly the inlets
//   flagged so, never a harbor;
// - each between two capes: across its axis at its site (up the coast signal of the geography),
//   land on either side within its width (the capes may be low points of rock, with beaches);
// - the same sites whatever the boxes (a box is the union of its quarters), the threads and the
//   order of the queries.
#include "terrain/generation/coast.hpp"
#include "terrain/generation/heightfield.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <thread>
#include <tuple>
#include <vector>

namespace {

using terrain::CoastSite;
using terrain::Heightfield;
using terrain::InletKind;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
constexpr double kHalfBox = 50'000.0;
// The capes are checked on the first kCapes anchorages of each box.
constexpr std::size_t kCapes = 15;

// Coasts of rocky shores, cliffs and calanques, then of fjords (the tests of the stop points).
constexpr std::array<std::array<double, 2>, 5> kCoasts{{{23'433'101.0, -2'128'828.0},
                                                        {-15'720'496.0, -6'266'783.0},
                                                        {-26'486'937.0, 7'219'391.0},
                                                        {1'793'548.0, 9'821'452.0},
                                                        {3'624'471.0, -10'006'796.0}}};

bool same(const CoastSite& a, const CoastSite& b) {
    return std::tie(a.x, a.z, a.opening, a.size, a.length, a.depth, a.shelter, a.inlet, a.coast,
                    a.harbor, a.anchorage) == std::tie(b.x, b.z, b.opening, b.size, b.length,
                                                       b.depth, b.shelter, b.inlet, b.coast,
                                                       b.harbor, b.anchorage);
}

bool sameSites(const std::vector<CoastSite>& a, const std::vector<CoastSite>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), same);
}

std::vector<CoastSite> anchoragesAround(const Heightfield& f, const std::array<double, 2>& c) {
    return f.anchoragesIn(c[0] - kHalfBox, c[1] - kHalfBox, c[0] + kHalfBox, c[1] + kHalfBox);
}

bool anchorageRule(const CoastSite& s) {
    return s.inlet == InletKind::Cove && !s.harbor && s.depth >= terrain::kHarborDepth &&
           s.opening >= terrain::kHarborOpening &&
           s.length >= terrain::kAnchorageLength * s.opening;
}

// How far the water runs from (x, z) toward angle t, radians (2 km at most).
double waterRun(const Heightfield& f, double x, double z, double t) {
    double d = 0.0;
    while (d < 2'000.0 &&
           f.coastAt(x + std::cos(t) * (d + 5.0), z + std::sin(t) * (d + 5.0)).signal < 0.0) {
        d += 5.0;
    }
    return d;
}

// A cape on the side (nx, nz) of the water through (x, z): land within reach meters.
bool capeOn(const Heightfield& f, double x, double z, double nx, double nz, double reach) {
    return waterRun(f, x, z, std::atan2(nz, nx)) <= reach;
}

// Between two capes: across the axis of the cove at its site (it goes up the coast signal of the
// geography), a cape on either side.
bool betweenCapes(const Heightfield& f, const CoastSite& s) {
    constexpr double h = 20.0;
    const double gx = f.coastAt(s.x + h, s.z).base - f.coastAt(s.x - h, s.z).base;
    const double gz = f.coastAt(s.x, s.z + h).base - f.coastAt(s.x, s.z - h).base;
    const double n = std::hypot(gx, gz);
    const double reach = static_cast<double>(s.size);
    return n > 0.0 && capeOn(f, s.x, s.z, -gz / n, gx / n, reach) &&
           capeOn(f, s.x, s.z, gz / n, -gx / n, reach);
}

void testAnchorages(const Heightfield& f) {
    int found = 0;
    bool flagged = true;
    bool listed = true;
    bool apart = true;
    int capes = 0;
    int checked = 0;
    for (const auto& c : kCoasts) {
        const std::vector<CoastSite> all =
            f.inletsIn(c[0] - kHalfBox, c[1] - kHalfBox, c[0] + kHalfBox, c[1] + kHalfBox);
        std::vector<CoastSite> expected;
        for (const CoastSite& s : all) {
            flagged = flagged && (!s.anchorage || anchorageRule(s));
            apart = apart && !(s.anchorage && s.harbor);
            if (s.anchorage) {
                expected.push_back(s);
            }
        }
        const std::vector<CoastSite> got = anchoragesAround(f, c);
        listed = listed && sameSites(got, expected);
        found += static_cast<int>(got.size());
        for (std::size_t k = 0; k < std::min(kCapes, got.size()); ++k) {
            ++checked;
            const bool between = betweenCapes(f, got[k]);
            capes += between ? 1 : 0;
            if (!between) {
                std::printf("  no capes at (%.0f, %.0f): opening %.0f m, widest %.0f m, length "
                            "%.0f m\n",
                            got[k].x, got[k].z, got[k].opening, got[k].size, got[k].length);
            }
        }
        const std::vector<CoastSite> harbors =
            f.harborsIn(c[0] - kHalfBox, c[1] - kHalfBox, c[0] + kHalfBox, c[1] + kHalfBox);
        for (const CoastSite& h : harbors) {
            apart = apart && std::none_of(got.begin(), got.end(),
                                          [&h](const CoastSite& a) { return same(a, h); });
        }
        std::printf("(%.0f, %.0f): %zu anchorages, %zu harbors\n", c[0], c[1], got.size(),
                    harbors.size());
    }
    std::printf("%d anchorages; %d of %d checked between two capes\n", found, capes, checked);
    TEST_CHECK(found > 0);
    TEST_CHECK(flagged);
    TEST_CHECK(listed);
    TEST_CHECK(apart);
    TEST_CHECK(checked > 0 && capes == checked);
}

// A box is the union of its quarters; the same lists whatever the threads and the order.
void testBoxes() {
    const Heightfield f(kSeed);
    bool union4 = true;
    for (const auto& c : kCoasts) {
        const std::vector<CoastSite> whole = anchoragesAround(f, c);
        std::vector<CoastSite> quarters;
        for (const double dz : {-kHalfBox, 0.0}) {
            for (const double dx : {-kHalfBox, 0.0}) {
                const std::vector<CoastSite> q = f.anchoragesIn(
                    c[0] + dx, c[1] + dz, c[0] + dx + kHalfBox, c[1] + dz + kHalfBox);
                quarters.insert(quarters.end(), q.begin(), q.end());
            }
        }
        std::sort(quarters.begin(), quarters.end(), [](const CoastSite& a, const CoastSite& b) {
            return std::tie(a.x, a.z, a.length) < std::tie(b.x, b.z, b.length);
        });
        std::vector<CoastSite> sorted = whole;
        std::sort(sorted.begin(), sorted.end(), [](const CoastSite& a, const CoastSite& b) {
            return std::tie(a.x, a.z, a.length) < std::tie(b.x, b.z, b.length);
        });
        union4 = union4 && sameSites(sorted, quarters);
    }
    const Heightfield fresh(kSeed);
    std::array<std::vector<CoastSite>, kCoasts.size()> got;
    std::vector<std::thread> threads;
    for (std::size_t t = 0; t < kCoasts.size(); ++t) {
        // Each thread its own coast, the coasts taken in reverse order.
        threads.emplace_back([&fresh, &got, t] {
            const std::size_t c = kCoasts.size() - 1 - t;
            got[c] = anchoragesAround(fresh, kCoasts[c]);
        });
    }
    for (std::thread& t : threads) {
        t.join();
    }
    bool threaded = true;
    for (std::size_t c = 0; c < kCoasts.size(); ++c) {
        threaded = threaded && sameSites(got[c], anchoragesAround(f, kCoasts[c]));
    }
    std::printf("boxes as the union of their quarters: %s; the same on %zu threads: %s\n",
                union4 ? "yes" : "no", kCoasts.size(), threaded ? "yes" : "no");
    TEST_CHECK(union4);
    TEST_CHECK(threaded);
}

} // namespace

int main() {
    const Heightfield f(kSeed);
    testAnchorages(f);
    testBoxes();
    return test::result();
}
