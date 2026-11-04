// The stacks of the coasts (agent 33) as the tiles of LOD 0 draw them, on the default world with
// its planet file (coast_planet_support.hpp): no cube and no lump of modelling clay (the checks in
// Unreal of d857a1b and e6f198d), but prisms of rock in beds, their faces nearly vertical meeting
// at sharp edges (never at a right angle), their tops flat or in steps, narrowing toward their top,
// notched where the sea beats them, angular blocks at their foot; and not one model repeated
// (rock_spires.hpp).
// Four stacks: the needle of the view of the stacks (coast_rock_support: kStack), a pillar, a stack
// split in two and an arch. Of each, around its middle, the rock of the tiles (a column holds rock
// at a height where the nearest surface over it faces up), from its foot to its top H (the
// highest ground of the tiles over it):
// - its outline at kLevels heights from 15 % to 75 % of H, every 5 degrees from its middle
//   (the furthest rock along each ray; around the middle of each section for its width, a spire
//   may lean): the root of the mean of its squares at 75 % kTaper of that at 15 % at most (it
//   narrows);
// - no face plane up its height: along each ray, the outline off its straight fit by kBent meters
//   at least (the root of the mean of the squares, over the rays);
// - its edges: the outline of every section of one piece, from its own middle, refined to kFine
//   meters along each ray, cut into straight runs (cornersOf): kSharpLeast sharp edges at least
//   over its sections, no right angle (the cubes of d857a1b had four a section, the rounded
//   outlines of e6f198d one or two sharp edges in all);
// - its beds: kLedges ledges at least along its rays (the outline stepping in by kLedge meters
//   within kLedgeRise meters of height, going up); its notch: on some ray, the outline at
//   kNotchLow meters over the sea kNotchDeep meters inside that at kNotchOver meters;
// - its top flat or in steps: its rock kFlatHigh meters under its top kFlat as wide as kFlatLow
//   meters under it at least (20 to 33 % on the domes of e6f198d); kBlocks blocks at least at its
//   foot (pieces of rock apart from it kBlockY meters over the sea);
// - its kind as its rock shows it: a passage under it (a column by its middle with air over the
//   sea up to a ceiling kPassage meters high at least: an arch), two pieces at 60 % of H
//   (split), its top kSlender of its foot at most (a needle), else a pillar: the four of them of
//   kKinds kinds at least.
#include "coast_planet_support.hpp"
#include "coast_rock_support.hpp"

#include "terrain/generation/coast.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <set>
#include <string>
#include <vector>

namespace {

using coast_rock::Tiles;

constexpr int kRays = 72;
constexpr int kMedian = 5;
constexpr std::array<double, 5> kLevels{0.15, 0.3, 0.45, 0.6, 0.75};
constexpr double kTaper = 0.85;
constexpr double kBent = 0.4;
constexpr double kStraight = 0.15;
constexpr double kRunLeast = 4.0;
constexpr double kRunGap = 4.0;
constexpr double kSharp = 30.0;
constexpr double kRightOff = 4.0;
constexpr double kRightGap = 1.5;
constexpr double kFlatHigh = 1.5;
constexpr double kFlatLow = 5.0;
constexpr double kBlockY = 2.0;
constexpr int kSharpLeast = 4;
constexpr double kFlat = 0.4;
constexpr int kBlocks = 3;
constexpr int kLedges = 2;
constexpr double kLedge = 0.8;
constexpr double kLedgeRise = 2.0;
constexpr double kBedsFrom = 7.0;
constexpr double kNotchLow = 1.0;
constexpr double kNotchOver = 5.5;
constexpr double kNotchDeep = 1.0;
constexpr double kPassage = 8.0;
constexpr double kPassageMiddle = 3.0;
constexpr double kPassageLow = 0.5;
constexpr double kSlender = 0.55;
constexpr std::size_t kKinds = 3;
constexpr double kReach = 45.0;
constexpr double kStep = 0.5;
constexpr double kFine = 0.03;

struct Named {
    const char* name;
    double x;
    double z;
};
constexpr std::array<Named, 4> kStacks{{{"the needle of the view", coast_rock::kStack.x,
                                         coast_rock::kStack.z},
                                        {"a pillar", -26'464'264.0, 7'231'922.0},
                                        {"a stack split in two", 23'228'361.0, -2'040'803.0},
                                        {"an arch", -26'488'669.0, 7'401'453.0}}};

// The stack of the reefs nearest to (x, z), 20 m high at least.
terrain::CoastStack stackNear(double x, double z) {
    terrain::CoastStack best;
    double nearest = 1e9;
    for (const terrain::CoastStack& s :
         coast_planet::field().stacksIn(x - 50.0, z - 50.0, x + 50.0, z + 50.0)) {
        const double d = std::hypot(s.x - x, s.z - z);
        if (s.height >= 20.0f && d < nearest) {
            nearest = d;
            best = s;
        }
    }
    return best;
}

// Whether the column at (x, z) holds rock at height y: the nearest surface over it faces up.
bool rockAt(Tiles& tiles, double x, double z, double y) {
    for (const coast_rock::Hit& h : tiles.hitsAt(x, z)) {
        if (h.y > y) {
            return h.up > 0.0;
        }
    }
    return false;
}

// The outline at height y: the furthest rock along each ray from (x, z) (0 where none).
std::vector<double> outlineAt(Tiles& tiles, double x, double z, double y) {
    std::vector<double> out(kRays, 0.0);
    for (int a = 0; a < kRays; ++a) {
        const double t = 2.0 * std::numbers::pi * a / kRays;
        for (double r = kReach; r >= 0.0; r -= kStep) {
            if (rockAt(tiles, x + r * std::cos(t), z + r * std::sin(t), y)) {
                out[static_cast<std::size_t>(a)] = r;
                break;
            }
        }
    }
    return out;
}

// The same, each ray refined to kFine meters (the faces of a section as straight as they are).
std::vector<double> fineOutlineAt(Tiles& tiles, double x, double z, double y) {
    std::vector<double> out = outlineAt(tiles, x, z, y);
    for (int a = 0; a < kRays; ++a) {
        double& r = out[static_cast<std::size_t>(a)];
        if (!(r > 0.0)) {
            continue;
        }
        const double t = 2.0 * std::numbers::pi * a / kRays;
        double in = r;
        double air = r + kStep;
        while (air - in > kFine) {
            const double mid = 0.5 * (in + air);
            (rockAt(tiles, x + mid * std::cos(t), z + mid * std::sin(t), y) ? in : air) = mid;
        }
        r = 0.5 * (in + air);
    }
    return out;
}

// The middle of the rock at height y within kReach of (x, z) (on a lattice of 1 m), and how many
// cells of it.
std::array<double, 3> middleAt(Tiles& tiles, double x, double z, double y) {
    double sx = 0.0, sz = 0.0, n = 0.0;
    for (double dz = -kReach; dz <= kReach; dz += 1.0) {
        for (double dx = -kReach; dx <= kReach; dx += 1.0) {
            if (rockAt(tiles, x + dx, z + dz, y)) {
                sx += x + dx;
                sz += z + dz;
                n += 1.0;
            }
        }
    }
    return n > 0.0 ? std::array<double, 3>{sx / n, sz / n, n} : std::array<double, 3>{x, z, 0.0};
}

double rmsOf(const std::vector<double>& v) {
    double sum = 0.0;
    for (const double a : v) {
        sum += a * a;
    }
    return std::sqrt(sum / static_cast<double>(v.size()));
}

// The corners of a section between its straight runs: the outline (its points along the rays
// from its middle, their radii the median of kMedian rays around) cut into runs of points within
// kStraight meters of the line through their ends, kRunLeast meters long at least; between two
// runs next to each other (kRunGap meters apart at most), the turn from one to the next: a sharp
// edge from kSharp degrees, a right angle within kRightOff of 90 between runs meeting within
// kRightGap meters (as a cube).
struct Corners {
    int sharp = 0;
    int right = 0;
};

Corners cornersOf(const std::vector<double>& raw) {
    std::vector<std::array<double, 2>> p(raw.size());
    for (int a = 0; a < kRays; ++a) {
        std::array<double, kMedian> around{};
        for (int k = 0; k < kMedian; ++k) {
            around[static_cast<std::size_t>(k)] =
                raw[static_cast<std::size_t>(((a + k - kMedian / 2) % kRays + kRays) % kRays)];
        }
        std::sort(around.begin(), around.end());
        const double t = 2.0 * std::numbers::pi * a / kRays;
        p[static_cast<std::size_t>(a)] = {around[kMedian / 2] * std::cos(t),
                                          around[kMedian / 2] * std::sin(t)};
    }
    const auto at = [&](int a) { return p[static_cast<std::size_t>((a % kRays + kRays) % kRays)]; };
    // Whether the points from a to b (b > a) lie within kStraight of the line through them.
    const auto straight = [&](int a, int b) {
        const auto u = at(a), v = at(b);
        const double len = std::hypot(v[0] - u[0], v[1] - u[1]);
        for (int k = a + 1; k < b && len > 0.0; ++k) {
            const auto w = at(k);
            const double cross = (v[0] - u[0]) * (w[1] - u[1]) - (v[1] - u[1]) * (w[0] - u[0]);
            const double off = std::abs(cross) / len;
            if (off > kStraight) {
                return false;
            }
        }
        return len > 0.0;
    };
    // The runs: from each start, as far as it stays straight.
    std::vector<std::array<int, 2>> runs;
    int a = 0;
    while (a < kRays) {
        int b = a + 1;
        while (b + 1 < a + kRays && straight(a, b + 1)) {
            ++b;
        }
        const auto u = at(a), v = at(b);
        if (std::hypot(v[0] - u[0], v[1] - u[1]) >= kRunLeast) {
            runs.push_back({a, b});
        }
        a = b;
    }
    Corners out;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const auto& r0 = runs[i];
        const auto& r1 = runs[(i + 1) % runs.size()];
        const auto e0 = at(r0[1]), s1 = at(r1[0]);
        if (runs.size() < 2 || std::hypot(s1[0] - e0[0], s1[1] - e0[1]) > kRunGap) {
            continue;
        }
        const auto a0 = at(r0[0]), b1 = at(r1[1]);
        const double u = std::atan2(e0[1] - a0[1], e0[0] - a0[0]);
        const double v = std::atan2(b1[1] - s1[1], b1[0] - s1[0]);
        double turn = std::abs(v - u) * 180.0 / std::numbers::pi;
        turn = std::min(turn, 360.0 - turn);
        out.sharp += turn >= kSharp ? 1 : 0;
        // A right angle between two faces meeting at it (not two corners on either side of a face
        // shorter than a run).
        const bool meet = std::hypot(s1[0] - e0[0], s1[1] - e0[1]) <= kRightGap;
        out.right += meet && std::abs(turn - 90.0) <= kRightOff ? 1 : 0;
    }
    return out;
}

// The off of the outline along each ray from its straight fit up the levels, as one root of the
// mean of their squares.
double bentOf(const std::vector<std::vector<double>>& outlines, const std::vector<double>& ys) {
    std::vector<double> residuals;
    for (int a = 0; a < kRays; ++a) {
        double sy = 0.0, sr = 0.0, syy = 0.0, syr = 0.0;
        const auto n = static_cast<double>(ys.size());
        for (std::size_t l = 0; l < ys.size(); ++l) {
            const double r = outlines[l][static_cast<std::size_t>(a)];
            sy += ys[l];
            sr += r;
            syy += ys[l] * ys[l];
            syr += ys[l] * r;
        }
        const double slope = (n * syr - sy * sr) / (n * syy - sy * sy);
        const double cut = (sr - slope * sy) / n;
        for (std::size_t l = 0; l < ys.size(); ++l) {
            residuals.push_back(outlines[l][static_cast<std::size_t>(a)] - cut - slope * ys[l]);
        }
    }
    return rmsOf(residuals);
}

struct Measures {
    double top = 0.0;
    double taper = 1.0;
    double bent = 0.0;
    int sharp = 0;
    int right = 0;
    double flat = 0.0;
    int blocks = 0;
    int ledges = 0;
    double notch = 0.0;
    bool passage = false;
    int pieces = 0;
    std::string kind;
};

// The highest ground of the tiles within kReach of (x, z), and where.
double topNear(double x, double z) {
    double top = -1e9;
    for (double dz = -kReach; dz <= kReach; dz += 1.0) {
        for (double dx = -kReach; dx <= kReach; dx += 1.0) {
            top = std::max(top,
                           static_cast<double>(coast_planet::tiles().groundAt(x + dx, z + dz)));
        }
    }
    return top;
}

// Its ledges (the most along a ray, every kStep up from its foot to 90 % of its top) and its
// notch (the most along a ray).
void bedsOf(Tiles& tiles, const terrain::CoastStack& s, Measures& m) {
    for (int a = 0; a < kRays; a += 2) {
        const double t = 2.0 * std::numbers::pi * a / kRays;
        const auto along = [&](double y) {
            for (double r = kReach; r >= 0.0; r -= 0.25) {
                if (rockAt(tiles, s.x + r * std::cos(t), s.z + r * std::sin(t), y)) {
                    return r;
                }
            }
            return 0.0;
        };
        int ledges = 0;
        double before = along(kBedsFrom);
        for (double y = kBedsFrom + kLedgeRise; y < 0.9 * m.top; y += kLedgeRise) {
            const double here = along(y);
            ledges += before - here >= kLedge ? 1 : 0;
            before = here;
        }
        m.ledges = std::max(m.ledges, ledges);
        m.notch = std::max(m.notch, along(kNotchOver) - along(kNotchLow));
    }
}

// How many pieces the rock makes at height y within kReach of (x, z), on a lattice of 1 m joined
// across their sides.
int piecesAt(Tiles& tiles, double x0, double z0, double y) {
    constexpr int kSide = 2 * static_cast<int>(kReach) + 1;
    std::vector<int> piece(static_cast<std::size_t>(kSide * kSide), -1);
    for (int k = 0; k < kSide; ++k) {
        for (int i = 0; i < kSide; ++i) {
            piece[static_cast<std::size_t>(k * kSide + i)] =
                rockAt(tiles, x0 + i - kReach, z0 + k - kReach, y) ? 0 : -1;
        }
    }
    int pieces = 0;
    for (std::size_t start = 0; start < piece.size(); ++start) {
        if (piece[start] != 0) {
            continue;
        }
        ++pieces;
        std::vector<std::size_t> todo{start};
        piece[start] = pieces;
        while (!todo.empty()) {
            const std::size_t c = todo.back();
            todo.pop_back();
            const int i = static_cast<int>(c) % kSide;
            const int k = static_cast<int>(c) / kSide;
            for (const auto& [di, dk] : std::array<std::array<int, 2>, 4>{
                     {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
                const int ni = i + di, nk = k + dk;
                const auto n = static_cast<std::size_t>(nk * kSide + ni);
                if (ni >= 0 && ni < kSide && nk >= 0 && nk < kSide && piece[n] == 0) {
                    piece[n] = pieces;
                    todo.push_back(n);
                }
            }
        }
    }
    return pieces;
}

// A passage under it: a column within kPassageMiddle meters of its middle with air kPassageLow
// meters over the sea and a ceiling kPassage meters high at least over it; and how many pieces
// its rock makes at 60 % of its top.
void kindOf(Tiles& tiles, const terrain::CoastStack& s, Measures& m) {
    for (double dz = -kPassageMiddle; dz <= kPassageMiddle; dz += 1.0) {
        for (double dx = -kPassageMiddle; dx <= kPassageMiddle; dx += 1.0) {
            for (const coast_rock::Hit& h : tiles.hitsAt(s.x + dx, s.z + dz)) {
                m.passage = m.passage || (h.up < 0.0 && h.y >= kPassage &&
                                          !rockAt(tiles, s.x + dx, s.z + dz, kPassageLow));
            }
        }
    }
    m.pieces = piecesAt(tiles, s.x, s.z, 0.6 * m.top);
}

Measures measure(const terrain::CoastStack& s) {
    Tiles tiles(coast_planet::tiles(), 0);
    Measures m;
    m.top = topNear(s.x, s.z);
    kindOf(tiles, s, m);
    std::vector<std::vector<double>> outlines;
    std::vector<std::vector<double>> middles; // around the middle of each section
    std::vector<double> ys;
    for (const double level : kLevels) {
        ys.push_back(level * m.top);
        outlines.push_back(outlineAt(tiles, s.x, s.z, ys.back()));
        // The turns of a section of one piece, around its own middle (a spire may lean).
        const std::array<double, 3> middle = middleAt(tiles, s.x, s.z, ys.back());
        const std::vector<double> around = fineOutlineAt(tiles, middle[0], middle[1], ys.back());
        const bool whole =
            std::all_of(around.begin(), around.end(), [](double r) { return r > 0.0; });
        const bool one = piecesAt(tiles, s.x, s.z, ys.back()) == 1;
        if (whole && one) {
            const Corners c = cornersOf(around);
            m.sharp += c.sharp;
            m.right += c.right;
        }
        middles.push_back(around);
    }
    m.taper = rmsOf(middles.back()) / std::max(1e-9, rmsOf(middles.front()));
    m.bent = bentOf(outlines, ys);
    bedsOf(tiles, s, m);
    m.flat = middleAt(tiles, s.x, s.z, m.top - kFlatHigh)[2] /
             std::max(1.0, middleAt(tiles, s.x, s.z, m.top - kFlatLow)[2]);
    m.blocks = piecesAt(tiles, s.x, s.z, kBlockY) - 1;
    m.kind = m.passage       ? "arch"
             : m.pieces >= 2 ? "split"
             : m.taper <= kSlender ? "needle"
                                   : "pillar";
    return m;
}

} // namespace

int main() {
    std::set<std::string> kinds;
    for (const Named& n : kStacks) {
        const terrain::CoastStack s = stackNear(n.x, n.z);
        TEST_CHECK(s.height >= 20.0f);
        const Measures m = measure(s);
        std::printf("%s (%.0f, %.0f), %.0f m drawn: its top %.1f m; at 75 %% %.0f %% as wide as at "
                    "15 %%, off its straight fit up its height by %.2f m, %d sharp edges and %d "
                    "right angles over its sections; %d ledges, a notch %.1f m deep; its top %.0f "
                    "%% as wide %.0f m under it as %.0f m under it; %d blocks at its foot; %d "
                    "pieces at 60 %%, %s passage: %s\n",
                    n.name, s.x, s.z, static_cast<double>(s.height), m.top, 100.0 * m.taper,
                    m.bent, m.sharp, m.right, m.ledges, m.notch, 100.0 * m.flat, kFlatHigh,
                    kFlatLow, m.blocks, m.pieces, m.passage ? "a" : "no", m.kind.c_str());
        TEST_CHECK(m.taper <= kTaper);
        TEST_CHECK(m.bent >= kBent);
        TEST_CHECK(m.sharp >= kSharpLeast && m.right == 0);
        TEST_CHECK(m.ledges >= kLedges);
        TEST_CHECK(m.notch >= kNotchDeep);
        TEST_CHECK(m.flat >= kFlat);
        TEST_CHECK(m.blocks >= kBlocks);
        kinds.insert(m.kind);
    }
    std::printf("%zu kinds of stacks among them\n", kinds.size());
    TEST_CHECK(kinds.size() >= kKinds);
    return test::result();
}
