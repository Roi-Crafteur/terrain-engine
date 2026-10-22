#pragma once

// The places of the views of terrain_export_view: summits, low ground around them, the floor of a
// great valley that sees a summit (view_k2) and the highest wall near it (view_wall).

#include "terrain/generation/heightfield.hpp"

#include <initializer_list>

namespace view_places {

struct Point {
    double x = 0.0;
    double z = 0.0;
    float height = 0.0f;
};

[[nodiscard]] Point pointAt(const terrain::Heightfield& field, double x, double z);

// The highest sample of a square of 2 half + 1 samples step meters apart around (x, z).
[[nodiscard]] Point highestAround(const terrain::Heightfield& field, double x, double z, int half,
                                  double step);

// The summit near a point: the highest point within 12 km at 500 m, then climbed to the top in
// steps of 100, 25 and 5 m (each repeated while it rises): crests are sharp.
[[nodiscard]] Point summitNear(const terrain::Heightfield& field, double x, double z);

// The lowest land (at least 1 m) at these distances from (x, z), in 64 directions; the point at
// the first distance due south when there is none.
[[nodiscard]] Point lowestAround(const terrain::Heightfield& field, double x, double z,
                                 std::initializer_list<double> distances);

// Heading in turns from (x0, z0) toward (x1, z1).
[[nodiscard]] double headingTo(double x0, double z0, double x1, double z1);

// The floor of a great valley seeing a summit: the lowest land 8 to 14 km from it (every 2 km, in
// 64 directions) from which the ground does not hide it, and the tangent of the angle at which
// the summit stands above the horizontal of an eye 1.8 m above that ground; found false when the
// ground hides the summit from all of them.
struct ValleyEye {
    Point eye;
    double tangent = 0.0;
    bool found = false;
};
[[nodiscard]] ValleyEye valleyEye(const terrain::Heightfield& field, const Point& summit);

// The highest wall within 4 km of a summit: the steepest step between neighbors 20 m apart of a
// lattice of 20 m (its foot the lower), the direction up it, the rise of the step and the height
// the ground rises within 400 m up from the foot.
struct Wall {
    Point foot;
    double upX = 1.0;
    double upZ = 0.0;
    double step = 0.0;
    double height = 0.0;
};
[[nodiscard]] Wall highestWall(const terrain::Heightfield& field, const Point& summit);

} // namespace view_places
