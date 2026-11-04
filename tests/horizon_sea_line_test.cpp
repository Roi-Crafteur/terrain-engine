// The line of the sea of the GPU horizon along the coasts (agent 40, after the checks in Unreal of
// the orchestrator: on the horizon, the edge of the water follows the grid, 1.6 m from the true
// shore on level 0, 16 m on level 3, and the water climbs up to 14.6 m on a wall at the view
// fjord_axe; white spikes at the foot of the walls, agent 33). On the default world with its
// planet file, at four places of the coasts, on the levels 0 to 5 where each one shows in the view
// (around the first true shore within kSector degrees of the heading, 64 to 128 pitches of the
// level from the eye, kHalf samples on either side; none: the level shows no coast in the view),
// every edge of every triangle of the grid (the axes and the diagonal of each cell,
// horizon_split.hpp):
// - the coast of the material (horizon_sea_line.hpp: edgeCoast) where it crosses the edge, and
//   how high above the sea level the ground shown stands there: the sea painted up the slope;
// - the true shore: where the ground of the tiles of LOD 0 (TerrainTiles::groundAt) crosses the sea
//   level along the same edge, nearest to that coast (within one edge on either side, every
//   kScan of the edge, then halved kRefine times): how far the coast of the material lies from it.
// Printed for the drawing of the material today (InGround) and for the sea drawn as a layer of its
// own (Layer). The checks: coast on kLeastLevels levels of every place at least, enough of it on
// each, the true shore found for most of it, and the Layer never paints the sea above the ground
// it shows.
#include "water_horizon_support.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

namespace {

using namespace water_horizon;

struct Place {
    const char* name;
    double x, z, heading;
};

constexpr std::array<Place, 4> kPlaces{{{"cote_fjord_axe", 1'819'677.0, 9'928'285.0, 70.0},
                                        {"falaise_cotiere", 4'944'509.0, -8'706'669.0, 225.0},
                                        {"cote_falaise_800m", 429'427.2, -855'233.8, 29.1},
                                        {"montagnes_mer", 1'430'074.0, 2'052'007.0, 133.0}}};
constexpr int kLevels = 6;
constexpr int kLeastLevels = 3;
constexpr double kSector = 30.0;
constexpr std::int64_t kHalf = 24;
constexpr double kScan = 1.0 / 32.0;
constexpr int kRefine = 8;
constexpr int kLeastCoast = 8;
constexpr double kLeastShore = 0.8;
constexpr std::size_t kMostShores = 48; // true shores sought per level of a place

struct Edge {
    double x0, z0, x1, z1;
    const HorizonTexel* a;
    const HorizonTexel* b;
};

// The edges of the triangles of a level: along x and z, and the diagonal of each cell (Fixed).
std::vector<Edge> edgesOf(const DrawnLevel& l) {
    std::vector<Edge> out;
    const auto x = [&](std::int64_t i) { return static_cast<double>(l.rect.x0 + i) * l.pitch; };
    const auto z = [&](std::int64_t k) { return static_cast<double>(l.rect.z0 + k) * l.pitch; };
    for (std::int64_t k = 0; k + 1 < l.rect.height(); ++k) {
        for (std::int64_t i = 0; i + 1 < l.rect.width(); ++i) {
            out.push_back({x(i), z(k), x(i + 1), z(k), &l.at(i, k), &l.at(i + 1, k)});
            out.push_back({x(i), z(k), x(i), z(k + 1), &l.at(i, k), &l.at(i, k + 1)});
            out.push_back({x(i + 1), z(k), x(i), z(k + 1), &l.at(i + 1, k), &l.at(i, k + 1)});
        }
    }
    return out;
}

double trueGround(double x, double z) {
    return static_cast<double>(coast_planet::tiles().groundAt(x, z));
}

// The true shore along the line of an edge nearest to its point t: its distance from it, meters;
// negative where none crosses within one edge on either side.
double shoreOffset(const Edge& e, double t) {
    const double length = std::hypot(e.x1 - e.x0, e.z1 - e.z0);
    const auto at = [&](double s) {
        return trueGround(e.x0 + s * (e.x1 - e.x0), e.z0 + s * (e.z1 - e.z0));
    };
    double best = -1.0;
    for (int side = -1; side <= 1; side += 2) {
        double s0 = t;
        double g0 = at(s0);
        for (double d = kScan; d <= 1.0 + 1e-9; d += kScan) {
            const double s1 = t + side * d;
            const double g1 = at(s1);
            if ((g0 < 0.0) != (g1 < 0.0)) {
                double lo = s0, hi = s1, glo = g0;
                for (int r = 0; r < kRefine; ++r) {
                    const double mid = 0.5 * (lo + hi);
                    const double gm = at(mid);
                    ((gm < 0.0) == (glo < 0.0) ? lo : hi) = mid;
                    glo = (gm < 0.0) == (glo < 0.0) ? gm : glo;
                }
                const double off = std::abs(0.5 * (lo + hi) - t) * length;
                best = best < 0.0 ? off : std::min(best, off);
                break;
            }
            s0 = s1;
            g0 = g1;
        }
    }
    return best;
}

struct Stats {
    int coast = 0;
    int shores = 0;
    int sought = 0;
    double paintedMost = 0.0;
    double paintedX = 0.0, paintedZ = 0.0;
    std::vector<double> painted;
    std::vector<double> offsets;
};

double quantile(std::vector<double> v, double q) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    return v[static_cast<std::size_t>(q * static_cast<double>(v.size() - 1))];
}

Stats measure(const DrawnLevel& l, SeaDrawing drawing) {
    Stats s;
    std::vector<std::pair<const Edge*, double>> found;
    const std::vector<Edge> edges = edgesOf(l);
    for (const Edge& e : edges) {
        const terrain::unreal::EdgeCoast c = terrain::unreal::edgeCoast(*e.a, *e.b, drawing);
        if (!c.found) {
            continue;
        }
        ++s.coast;
        s.painted.push_back(c.painted);
        if (c.painted > s.paintedMost) {
            s.paintedMost = c.painted;
            s.paintedX = e.x0 + c.t * (e.x1 - e.x0);
            s.paintedZ = e.z0 + c.t * (e.z1 - e.z0);
        }
        found.emplace_back(&e, c.t);
    }
    // The true shores of evenly spread coast points (their search reads the tiles).
    const std::size_t stride = std::max<std::size_t>(1, found.size() / kMostShores);
    for (std::size_t n = 0; n < found.size(); n += stride) {
        ++s.sought;
        const double off = shoreOffset(*found[n].first, found[n].second);
        if (off >= 0.0) {
            ++s.shores;
            s.offsets.push_back(off);
        }
    }
    return s;
}

// The first true shore along the rays from the eye within kSector degrees of the heading (the
// nearest to it first), from 64 to 128 pitches of the level: where the level shows in the view.
bool shoreInView(const Place& p, double pitch, double& x, double& z) {
    for (double off = 0.0; off <= kSector; off += 2.0) {
        for (const double side : {1.0, -1.0}) {
            const double rad = (p.heading + side * off) * std::numbers::pi / 180.0;
            const double dx = std::sin(rad), dz = -std::cos(rad);
            double before = trueGround(p.x + 64.0 * pitch * dx, p.z + 64.0 * pitch * dz);
            for (double d = 64.5 * pitch; d <= 128.0 * pitch; d += 0.5 * pitch) {
                const double here = trueGround(p.x + d * dx, p.z + d * dz);
                if ((before < 0.0) != (here < 0.0)) {
                    x = p.x + d * dx;
                    z = p.z + d * dz;
                    return true;
                }
                before = here;
            }
        }
    }
    return false;
}

void testPlace(const Place& p) {
    int levels = 0;
    for (int level = 0; level < kLevels; ++level) {
        const double pitch = terrain::levelPitch(params(), level);
        double x = 0.0, z = 0.0;
        if (!shoreInView(p, pitch, x, z)) {
            std::printf("%s, level %d (%3.0f m): no coast in the view\n", p.name, level, pitch);
            continue;
        }
        ++levels;
        const DrawnLevel l = drawnLevel(level, rectAround(level, x, z, kHalf), false);
        for (const SeaDrawing drawing : {SeaDrawing::InGround, SeaDrawing::Layer}) {
            const Stats s = measure(l, drawing);
            std::printf("%s, level %d (%3.0f m), %s: %4d coast edges; the sea painted up the "
                        "ground %.2f m at most (at %.0f ; %.0f), %.2f m at the 90th centile; the "
                        "true shore %.2f m away at the median, %.2f m at the 90th centile, %.2f m "
                        "at most (%d of %d found)\n",
                        p.name, level, pitch,
                        drawing == SeaDrawing::InGround ? "in the ground" : "a layer",
                        s.coast, s.paintedMost, s.paintedX, s.paintedZ, quantile(s.painted, 0.9),
                        quantile(s.offsets, 0.5), quantile(s.offsets, 0.9),
                        quantile(s.offsets, 1.0), s.shores, s.sought);
            TEST_CHECK(s.coast >= kLeastCoast);
            TEST_CHECK(s.shores >= kLeastShore * s.sought);
            if (drawing == SeaDrawing::Layer) {
                TEST_CHECK(s.paintedMost == 0.0);
            }
        }
    }
    TEST_CHECK(levels >= kLeastLevels);
}

} // namespace

int main() {
    for (const Place& p : kPlaces) {
        testPlace(p);
    }
    return test::result();
}
