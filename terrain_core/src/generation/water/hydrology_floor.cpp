// How far a point lies from the floor of the valleys of the rivers (Hydrology::floorDistance): what
// the seas of dunes keep clear of (ergs.hpp).
#include "hydrology_impl.hpp"
#include "river_profile.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace terrain {

namespace {
// How far the floor of a valley may reach from the straight line of its reach (beyond kStray):
// a floodplain is kilometers wide. reachesNear finds the reaches by that line only; a point on a
// wide floor lies farther from it than the limit, and a search by the limit alone would miss the
// floor under it (a jump of the result along a line).
constexpr double kFloorReach = 8'000.0;

// The reaches whose floor may lie within limit of a point of the cell: those near its center,
// half a diagonal farther. Kept for the last cell on each thread (a pure function of the cell and
// the limit: more reaches than needed never change the nearest floor).
const std::vector<std::size_t>& reachesOfCell(const Hydrology& h, std::uint64_t id,
                                              const PlanetGrid& grid, double x, double z,
                                              double limit) {
    struct Last {
        std::uint64_t owner = 0;
        std::size_t cell = 0;
        double limit = -1.0;
        std::vector<std::size_t> reaches;
    };
    thread_local Last last;
    const std::size_t cell = planetCellAt(grid, x, z);
    if (last.owner != id || last.cell != cell || last.limit != limit) {
        constexpr double kHalfDiagonal = 0.7072;
        last.reaches = h.reachesNear(grid.centerX(grid.column(cell)), grid.centerZ(grid.row(cell)),
                                     limit + kFloorReach + kHalfDiagonal * grid.cellSize);
        last.owner = id;
        last.cell = cell;
        last.limit = limit;
    }
    return last.reaches;
}

} // namespace

double Hydrology::floorDistance(double x, double z, double limit) const {
    const Impl& m = *impl_;
    double best = limit;
    for (const std::size_t r : reachesOfCell(*this, m.memoId, m.grid, x, z, limit)) {
        const CourseRef held = m.courseOf(r);
        const water::Course& c = *held;
        const water::PathPoint a = m.startOf(r);
        // The point in the images of the course.
        const double px = a.x + noise::wrapCanonical(x - a.x, m.grid.periodX());
        const double pz = a.z + noise::wrapCanonical(z - a.z, m.grid.periodZ());
        for (const water::CourseChunk& k : c.chunks) {
            if (dmath::hypot(px - k.x, pz - k.z) - k.radius - k.flat > best) {
                continue;
            }
            for (std::uint32_t i = k.first; i < k.last && i + 1 < c.points.size(); ++i) {
                const water::CoursePoint& p = c.points[i];
                const water::CoursePoint& q = c.points[i + 1];
                const double floor =
                    std::max({static_cast<double>(p.floorHalf), static_cast<double>(q.floorHalf),
                              static_cast<double>(std::max(p.half, q.half) +
                                                  std::max(std::abs(p.meander), std::abs(q.meander)))});
                const double d = water::distanceToSegment(px, pz, p.x, p.z, q.x, q.z) - floor;
                best = std::min(best, std::max(0.0, d));
            }
        }
    }
    return best;
}

} // namespace terrain
