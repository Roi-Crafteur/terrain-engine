// The inner gorges and the temples of the deep canyons of the plateaus (agent 46,
// tableland_canyons.hpp, tableland_temples.hpp) on the default world switched on:
//   - no canal: the axis of a deep canyon and the two edges of its inner gorge (the zero lines of
//     Fields::axis and Fields::gorge), followed kLineLength meters each way where the canyon is
//     kDeepCut meters deep at least, have no stretch longer than kStraightMost within kTolerance of
//     its chord; the width of the gorge along the axis (twice Fields::gorge there) varies by
//     kLeastWidthVariation at least (its coefficient of variation);
//   - no cube: the temples around the floor, the rim of their lower storey (the zero line of
//     Fields::templeOutline), its share in straight stretches longer than kStraightLeast (within
//     kTempleTolerance of their chord, turning by kTurnMost at most: flat sides, not the arcs of a
//     wide temple) kStraightShareMost at most over all of them (by length, the worst printed); their top kTopShareMost of their
//     footprint at most, and two storeys (two levels of flat ground over the foot of their talus),
//     on kStoreyedShare of them at least.
#include "pattern_support.hpp"
#include "tablelands_fields_support.hpp"
#include "tablelands_lines_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using tablelands_fields::Fields;
using tablelands_fields::fieldsAt;
using tablelands_lines::gradientOf;
using tablelands_lines::longestStraight;
using tablelands_lines::Point;
using tablelands_lines::straightShare;
using tablelands_lines::zeroLine;

// The canyons: the deepest floors inland (kInland meters from the escarpment) of the thickest
// plateaus, kCanyonDeep meters deep at least, kMostCanyons of them.
constexpr double kCell = 250.0;
constexpr double kInland = 6'000.0;
constexpr double kCanyonDeep = 1'000.0;
constexpr std::size_t kMostCanyons = 3;
// Their lines: followed kLineStep meters at a time, kLineLength each way, kept where the canyon cuts
// kDeepCut meters at least; straight within kTolerance over kStraightMost at most.
constexpr double kLineStep = 25.0;
constexpr double kLineLength = 6'000.0;
constexpr double kDeepCut = 500.0;
constexpr double kTolerance = 50.0;
constexpr double kStraightMost = 2'000.0;
constexpr double kLeastWidthVariation = 0.3;
// The temples: cells of kTempleCell meters within kTempleReach cells of the floor, groups of
// kTempleCells at least; their rim followed kRimStep at a time; their shape on cells of kShapeCell.
constexpr double kTempleCell = 50.0;
constexpr int kTempleReach = 40;
constexpr std::size_t kTempleCells = 4;
constexpr double kRimStep = 5.0;
constexpr double kStraightLeast = 200.0;
constexpr double kTempleTolerance = 15.0;
constexpr double kTurnMost = 10.0 / 57.29577951308232;
constexpr double kStraightShareMost = 0.1;
constexpr double kShapeCell = 5.0;
constexpr double kTopBand = 5.0;
constexpr double kTopShareMost = 0.5;
constexpr double kFlat = 0.2;
constexpr double kLevelGap = 20.0;
constexpr std::size_t kLevelCells = 5;
constexpr double kStoreyedShare = 0.5;

std::array<double, 3> deepestFloor(double x0, double z0, int side) {
    std::array<double, 3> best{x0, z0, 0.0};
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            const double x = x0 + kCell * (i - side / 2);
            const double z = z0 + kCell * (j - side / 2);
            const Fields f = fieldsAt(x, z);
            if (f.floor >= 1.0 && f.canyon > best[2] && f.scarp > kInland) {
                best = {x, z, f.canyon};
            }
        }
    }
    return best;
}

// The longest run of a line where the canyon cuts kDeepCut at least.
std::vector<Point> deepRun(const std::vector<Point>& line) {
    std::vector<Point> best;
    std::vector<Point> run;
    for (const Point& p : line) {
        if (fieldsAt(p.x, p.z).canyon >= kDeepCut) {
            run.push_back(p);
            best = run.size() > best.size() ? run : best;
        } else {
            run.clear();
        }
    }
    return best;
}

double axisAt(double x, double z) {
    return fieldsAt(x, z).axis;
}

double gorgeAt(double x, double z) {
    return fieldsAt(x, z).gorge;
}

// An edge of the gorge from the axis point p, on the side side: across the axis, by bisection.
bool edgeFrom(const Point& p, double side, Point& out) {
    const Point g = gradientOf(axisAt, p, 5.0);
    const double norm = std::max(std::hypot(g.x, g.z), 1e-12);
    const Point n{side * g.x / norm, side * g.z / norm};
    double lo = 0.0;
    double hi = 0.0;
    for (double d = 25.0; d <= 4'000.0; d += 25.0) {
        if (gorgeAt(p.x + n.x * d, p.z + n.z * d) < 0.0) {
            hi = d;
            break;
        }
        lo = d;
    }
    if (!(hi > 0.0)) {
        return false;
    }
    for (int k = 0; k < 20; ++k) {
        const double mid = 0.5 * (lo + hi);
        (gorgeAt(p.x + n.x * mid, p.z + n.z * mid) >= 0.0 ? lo : hi) = mid;
    }
    out = {p.x + n.x * lo, p.z + n.z * lo};
    return true;
}

// The straightest stretch of a line, meters, printed with its name.
double straightOf(const std::vector<Point>& line, const char* name) {
    Point from;
    const double straight = longestStraight(line, kLineStep, kTolerance, from);
    std::printf("  %s: %.1f km followed, its longest straight stretch (within 50 m) %.0f m\n", name,
                kLineStep * static_cast<double>(line.size()) / 1000.0, straight);
    return straight;
}

// The gorge of the canyon of floor (x, z): the longest straight stretch of its axis and edges, and
// the variation of its width along its axis (-1 if not followed).
std::array<double, 2> gorgeOf(double x, double z) {
    Point start;
    if (!tablelands_lines::zeroNear(axisAt, {x, z}, 3'000.0, start)) {
        return {0.0, -1.0};
    }
    const std::vector<Point> axis = deepRun(zeroLine(axisAt, start, kLineStep, kLineLength, 5.0, 10.0));
    if (axis.size() < 40) {
        return {0.0, -1.0};
    }
    double straight = straightOf(axis, "axis");
    std::vector<double> widths;
    for (std::size_t i = 0; i < axis.size(); i += 4) {
        widths.push_back(2.0 * gorgeAt(axis[i].x, axis[i].z));
    }
    const Point middle = axis[axis.size() / 2];
    for (const double side : {-1.0, 1.0}) {
        Point edge;
        if (!edgeFrom(middle, side, edge)) {
            continue;
        }
        const std::vector<Point> line =
            deepRun(zeroLine(gorgeAt, edge, kLineStep, kLineLength, 5.0, 10.0));
        straight = std::max(straight, straightOf(line, side < 0.0 ? "left edge" : "right edge"));
    }
    return {straight, pattern_test::variation(widths)};
}

// What a temple shows: the share of its rim in straight stretches, its top over its footprint, how
// many levels of flat ground it holds.
struct TempleShape {
    double straight = 0.0;
    double rim = 0.0; // meters of rim followed
    double top = 0.0;
    int levels = 0;
};

// The rim from start once round: the forward half of the line from start, up to where it comes back
// near start (a closed rim); the whole line if it never does.
std::vector<Point> onceRound(const std::vector<Point>& line, const Point& start) {
    std::size_t from = 0;
    while (from < line.size() && !(line[from].x == start.x && line[from].z == start.z)) {
        ++from;
    }
    std::vector<Point> out;
    for (std::size_t i = from; i < line.size(); ++i) {
        out.push_back(line[i]);
        if (out.size() > 20 && std::hypot(line[i].x - start.x, line[i].z - start.z) < 2.0 * kRimStep) {
            return out;
        }
    }
    return from < line.size() ? line : out;
}

double templeField(double x, double z) {
    const Fields f = fieldsAt(x, z);
    return f.temple ? f.templeOutline : -50.0;
}

// The levels of flat ground (depths under the rim) among those of flat cells.
int levelsOf(std::vector<double> depths) {
    std::sort(depths.begin(), depths.end());
    int levels = 0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < depths.size(); ++i) {
        count = i > 0 && depths[i] - depths[i - 1] > kLevelGap ? 1 : count + 1;
        levels += count == kLevelCells ? 1 : 0;
    }
    return levels;
}

TempleShape shapeOf(const std::vector<Point>& cells) {
    TempleShape out;
    Point inside = cells.front();
    double deepest = -1e9;
    double minX = 1e18;
    double maxX = -1e18;
    double minZ = 1e18;
    double maxZ = -1e18;
    for (const Point& c : cells) {
        const double v = templeField(c.x, c.z);
        inside = v > deepest ? c : inside;
        deepest = std::max(deepest, v);
        minX = std::min(minX, c.x - kTempleCell);
        maxX = std::max(maxX, c.x + kTempleCell);
        minZ = std::min(minZ, c.z - kTempleCell);
        maxZ = std::max(maxZ, c.z + kTempleCell);
    }
    // Its rim: from the most inner cell along x, then followed.
    Point rim;
    if (tablelands_lines::zeroNear(templeField, inside, 2'000.0, rim)) {
        const std::vector<Point> line =
            onceRound(zeroLine(templeField, rim, kRimStep, 3'000.0, 1.0, 2.0, 10.0), rim);
        out.straight = straightShare(line, kRimStep, kTempleTolerance, kStraightLeast, kTurnMost);
        out.rim = kRimStep * static_cast<double>(line.size());
    }
    // Its shape on cells of kShapeCell: its footprint, its top, its flat ground.
    std::vector<double> depths;
    std::vector<double> flats;
    const int nx = static_cast<int>((maxX - minX) / kShapeCell) + 1;
    const int nz = static_cast<int>((maxZ - minZ) / kShapeCell) + 1;
    std::vector<double> grid(static_cast<std::size_t>(nx * nz), -1.0);
    for (int j = 0; j < nz; ++j) {
        for (int i = 0; i < nx; ++i) {
            const Fields f = fieldsAt(minX + kShapeCell * i, minZ + kShapeCell * j);
            grid[static_cast<std::size_t>(j * nx + i)] = f.temple ? f.canyon : -1.0;
        }
    }
    double top = 1e18;
    double foot = 0.0;
    for (const double d : grid) {
        if (d >= 0.0) {
            depths.push_back(d);
            top = std::min(top, d);
            foot = std::max(foot, d);
        }
    }
    for (int j = 1; j + 1 < nz; ++j) {
        for (int i = 1; i + 1 < nx; ++i) {
            const auto at = [&](int a, int b) { return grid[static_cast<std::size_t>(b * nx + a)]; };
            const double d = at(i, j);
            if (d < 0.0 || at(i + 1, j) < 0.0 || at(i - 1, j) < 0.0 || at(i, j + 1) < 0.0 ||
                at(i, j - 1) < 0.0) {
                continue;
            }
            const double slope = std::hypot(at(i + 1, j) - at(i - 1, j), at(i, j + 1) - at(i, j - 1)) /
                                 (2.0 * kShapeCell);
            if (slope < kFlat && d < foot - kLevelGap) {
                flats.push_back(d);
            }
        }
    }
    const auto topCells = std::count_if(depths.begin(), depths.end(),
                                        [top](double d) { return d <= top + kTopBand; });
    out.top = depths.empty() ? 1.0
                             : static_cast<double>(topCells) / static_cast<double>(depths.size());
    out.levels = levelsOf(flats);
    return out;
}

// The temples around the floor (x0, z0): groups of cells where one stands.
std::vector<std::vector<Point>> templesAround(double x0, double z0) {
    const int side = 2 * kTempleReach + 1;
    std::vector<int> cells(static_cast<std::size_t>(side * side), 0);
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            cells[static_cast<std::size_t>(j * side + i)] =
                fieldsAt(x0 + kTempleCell * (i - kTempleReach), z0 + kTempleCell * (j - kTempleReach))
                        .temple
                    ? 1
                    : 0;
        }
    }
    std::vector<std::vector<Point>> out;
    for (int start = 0; start < side * side; ++start) {
        if (cells[static_cast<std::size_t>(start)] != 1) {
            continue;
        }
        std::vector<Point> group;
        std::vector<int> stack{start};
        cells[static_cast<std::size_t>(start)] = 2;
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            group.push_back({x0 + kTempleCell * (c % side - kTempleReach),
                             z0 + kTempleCell * (c / side - kTempleReach)});
            for (const int d : {1, -1, side, -side}) {
                const int nc = c + d;
                const bool row = d == 1 || d == -1 ? nc / side == c / side : true;
                if (nc >= 0 && nc < side * side && row && cells[static_cast<std::size_t>(nc)] == 1) {
                    cells[static_cast<std::size_t>(nc)] = 2;
                    stack.push_back(nc);
                }
            }
        }
        if (group.size() >= kTempleCells) {
            out.push_back(std::move(group));
        }
    }
    return out;
}

} // namespace

int main() {
    const tablelands_fields::Sites sites = tablelands_fields::findSites();
    int canyons = 0;
    double straightest = 0.0;
    double leastVariation = 1e9;
    int temples = 0;
    double rims = 0.0;
    double straightRims = 0.0;
    int topped = 0;
    int storeyed = 0;
    double straightMost = 0.0;
    for (std::size_t t = 0; t < sites.thick.size() && static_cast<std::size_t>(canyons) < kMostCanyons;
         ++t) {
        const std::array<double, 3> f = deepestFloor(sites.thick[t][0], sites.thick[t][1], 120);
        if (!(f[2] >= kCanyonDeep)) {
            continue;
        }
        std::printf("canyon at (%.0f, %.0f), %.0f m deep:\n", f[0], f[1], f[2]);
        const std::array<double, 2> gorge = gorgeOf(f[0], f[1]);
        if (gorge[1] < 0.0) {
            continue;
        }
        ++canyons;
        straightest = std::max(straightest, gorge[0]);
        leastVariation = std::min(leastVariation, gorge[1]);
        std::printf("  the width of its gorge varying by %.2f along its axis\n", gorge[1]);
        for (const std::vector<Point>& cells : templesAround(f[0], f[1])) {
            const TempleShape s = shapeOf(cells);
            ++temples;
            straightMost = std::max(straightMost, s.straight);
            rims += s.rim;
            straightRims += s.straight * s.rim;
            topped += s.top <= kTopShareMost ? 1 : 0;
            storeyed += s.levels >= 2 ? 1 : 0;
            std::printf("  temple near (%.0f, %.0f): its rim %.0f %% straight, its top %.0f %% of it, "
                        "%d levels\n",
                        cells.front().x, cells.front().z, 100.0 * s.straight, 100.0 * s.top, s.levels);
        }
    }
    std::printf("gorges: %d canyons, the longest straight stretch of their axes and edges %.0f m, "
                "their widths varying by %.2f at the least; temples: %d, %.1f km of rims %.1f %% "
                "straight (the worst %.0f %%), %d with a top under %.0f %% of them, %d of two "
                "storeys\n",
                canyons, straightest, leastVariation, temples, rims / 1000.0,
                100.0 * straightRims / std::max(rims, 1.0), 100.0 * straightMost, topped,
                100.0 * kTopShareMost, storeyed);
    TEST_CHECK(canyons >= 1);
    TEST_CHECK(straightest < kStraightMost);
    TEST_CHECK(leastVariation >= kLeastWidthVariation);
    TEST_CHECK(temples >= 3);
    TEST_CHECK(straightRims <= kStraightShareMost * rims);
    TEST_CHECK(static_cast<double>(topped) >= kStoreyedShare * static_cast<double>(temples));
    TEST_CHECK(static_cast<double>(storeyed) >= kStoreyedShare * static_cast<double>(temples));
    return test::result();
}
