// The section rock of terrain_probe (probe_sections.hpp): the steepest slope at the point and the
// solid and empty intervals of the column of the lattice nearest to it, from the density of the
// streamed world (TerrainTiles::densityAt: the relief, the caves with --caves, the edits). Each
// end of a solid interval lies where the density crosses 0 between two lattice points, linearly,
// as Marching Cubes places the surface. The faces of the 3D rock (phase 12A) will come in
// "faces"; absent until then.
#include "probe_sections.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <cmath>

namespace probe {

namespace {

constexpr int kBelow = 32; // meters of the column scanned under the ground
constexpr int kAbove = 64; // and over it

double steepness(const ProbeWorld& w, const Point& p) {
    const terrain::Heightfield& f = w.field();
    const double h = p.ground;
    double steepest = 0.0;
    constexpr double kSides[4][2] = {{1.0, 0.0}, {-1.0, 0.0}, {0.0, 1.0}, {0.0, -1.0}};
    for (const auto& side : kSides) {
        const double n = f.heightAt(wrapX(w, p.x + side[0]), wrapZ(w, p.z + side[1]));
        steepest = std::max(steepest, std::abs(n - h));
    }
    return steepest; // the lattice step is 1 m
}

// The height where the density crosses 0 between y (density a) and y + 1 (density b).
double crossing(int y, float a, float b) {
    return static_cast<double>(y) + static_cast<double>(a) / static_cast<double>(a - b);
}

Record interval(bool bottomKnown, double bottom, bool topKnown, double top) {
    Record r;
    if (bottomKnown) {
        r.number("bottom", bottom, 3, "m");
    } else {
        r.none("bottom");
    }
    if (topKnown) {
        r.number("top", top, 3, "m");
    } else {
        r.none("top");
    }
    return r;
}

} // namespace

Record rockSection(const ProbeWorld& w, const Point& p) {
    const int x = static_cast<int>(std::lround(p.x));
    const int z = static_cast<int>(std::lround(p.z));
    const int from = static_cast<int>(std::floor(p.ground)) - kBelow;
    const int to = static_cast<int>(std::floor(p.ground)) + kAbove;
    std::vector<Record> solids;
    float previous = w.tiles().densityAt(x, from, z);
    bool inside = previous < 0.0f;
    double bottom = 0.0;
    bool bottomKnown = false;
    for (int y = from + 1; y <= to; ++y) {
        const float d = w.tiles().densityAt(x, y, z);
        if ((d < 0.0f) != inside) {
            const double at = crossing(y - 1, previous, d);
            if (inside) {
                solids.push_back(interval(bottomKnown, bottom, true, at));
            } else {
                bottom = at;
                bottomKnown = true;
            }
            inside = !inside;
        }
        previous = d;
    }
    if (inside) {
        solids.push_back(interval(bottomKnown, bottom, false, 0.0));
    }
    const float atGround = w.tiles().densityAt(x, static_cast<int>(std::lround(p.ground)), z);
    Record r;
    r.number("steepest", slopeDegrees(steepness(w, p)), 2, "deg")
        .integer("columnX", x)
        .integer("columnZ", z)
        .integer("scanFrom", from)
        .integer("scanTo", to)
        .number("densityAtGround", atGround, 3)
        .list("solid", std::move(solids));
    return r.none("faces");
}

} // namespace probe
