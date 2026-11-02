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

double Hydrology::floorDistance(double x, double z, double limit) const {
    const Impl& m = *impl_;
    double best = limit;
    for (const std::size_t r : reachesNear(x, z, limit)) {
        const water::Course& c = m.courseOf(r);
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
