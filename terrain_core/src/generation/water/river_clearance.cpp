// How far the valleys, ponds and lakes of the rivers keep from a point (Hydrology::clearance): what
// the coasts (agent 33) need to leave the corridors of the rivers every bit of the relief the
// rivers were found over.
#include "terrain/generation/water/hydrology.hpp"

#include "fine_lake.hpp"
#include "hydrology_impl.hpp"
#include "river_profile.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace terrain {

namespace {

using water_detail::kSearch;
using water_detail::kValleyReach;

// The signed distance from (x, z), at its image nearest to the window of a lake, to that window
// (negative inside), infinite for a lake without water.
double windowClearance(const water::FineLake& lake, double x, double z) {
    const water::LakeWindow w = lake.window();
    if (w.x1 < w.x0) {
        return std::numeric_limits<double>::infinity();
    }
    const double cx = 0.5 * (w.x0 + w.x1);
    const double cz = 0.5 * (w.z0 + w.z1);
    const double dx = std::abs(noise::wrapCanonical(x - cx, lake.periodX())) - 0.5 * (w.x1 - w.x0);
    const double dz = std::abs(noise::wrapCanonical(z - cz, lake.periodZ())) - 0.5 * (w.z1 - w.z0);
    return std::max(dx, dz);
}

// The clearance of a course at (px, pz) in its images over relief ground meters high: of each of
// its chunks, the least of how far beyond its floors the point lies and how far above the relief
// raised by the levees and the blend its gentlest walls stand there (valleyAt skips the chunk
// wherever both are positive); of each of its ponds, the distance to its window.
double courseClearance(const water::Course& c, double px, double pz, double ground) {
    double least = std::numeric_limits<double>::infinity();
    for (const water::FineLake& pond : c.ponds) {
        least = std::min(least, windowClearance(pond, px, pz));
    }
    if (px < c.minX || px > c.maxX || pz < c.minZ || pz > c.maxZ) {
        return least; // valleyAt finds no valley out of the box of the course
    }
    const double bound = ground + water::kLevee + water::kBlend;
    for (const water::CourseChunk& k : c.chunks) {
        const double away = dmath::hypot(px - k.x, pz - k.z) - k.radius - k.flat;
        const double floorOf = static_cast<double>(k.minLevel - k.maxDepth) +
                               static_cast<double>(k.minWall) * std::max(0.0, away);
        least = std::min(least, std::min(away, floorOf - bound));
    }
    return least;
}

} // namespace

bool Hydrology::mayMatter(double x, double z) const {
    const Impl& m = *impl_;
    const std::size_t cell = planetCellAt(m.grid, x, z);
    const int ci = m.grid.column(cell);
    const int cj = m.grid.row(cell);
    for (int dj = -kSearch; dj <= kSearch; ++dj) {
        for (int di = -kSearch; di <= kSearch; ++di) {
            const Impl::CellReaches here = m.reachesOfCell(
                m.grid.index(m.grid.wrapColumn(ci + di), m.grid.wrapRow(cj + dj)));
            for (std::size_t k = 0; k < here.count; ++k) {
                if (m.mayReach(here.reach[k], x, z, kValleyReach)) {
                    return true;
                }
            }
        }
    }
    return m.lakesAround(cell).count > 0;
}

double Hydrology::clearance(double x, double z, double ground) const {
    const Impl& m = *impl_;
    const std::size_t cell = planetCellAt(m.grid, x, z);
    const int ci = m.grid.column(cell);
    const int cj = m.grid.row(cell);
    double least = std::numeric_limits<double>::infinity();
    for (int dj = -kSearch; dj <= kSearch; ++dj) {
        for (int di = -kSearch; di <= kSearch; ++di) {
            const Impl::CellReaches here = m.reachesOfCell(
                m.grid.index(m.grid.wrapColumn(ci + di), m.grid.wrapRow(cj + dj)));
            for (std::size_t k = 0; k < here.count; ++k) {
                const std::size_t r = here.reach[k];
                if (!m.mayReach(r, x, z, kValleyReach)) {
                    continue;
                }
                const water::PathPoint start = m.startOf(r);
                const double px = start.x + noise::wrapCanonical(x - start.x, m.grid.periodX());
                const double pz = start.z + noise::wrapCanonical(z - start.z, m.grid.periodZ());
                least = std::min(least, courseClearance(m.courseOf(r), px, pz, ground));
            }
        }
    }
    const LakeList around = m.lakesAround(cell);
    for (std::size_t k = 0; k < around.count; ++k) {
        least = std::min(least, windowClearance(m.fineLakeOf(around.ids[k]), x, z));
    }
    return least;
}

} // namespace terrain
