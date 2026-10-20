#pragma once

#include "terrain/generation/caves.hpp"

namespace terrain {

// Geometry of the surface entrances (CaveField::entrance places them).

// Columns farther than the largest radius of an entrance plus this margin from its axis
// ignore it: there its void is at least the margin, beyond the guard and fade of every detail
// carving entrances (at most 15 m, on the 4 m cells of LOD 2), so it would not change the
// density.
inline constexpr double kEntranceReachMargin = 16.0;

// Horizontal distance from (x, z) to the axis of the entrance: the segment from its top to
// its bottom, seen from above.
[[nodiscard]] double entranceAxisDistance(const CaveEntrance& entrance, double x, double z);

// True if the column at (x, z) crosses the tube or the chamber of the entrance, widened by
// margin: within tubeRadius + margin of its axis or chamberRadius + margin of its chamber,
// horizontally.
[[nodiscard]] bool entranceColumn(const CaveEntrance& entrance, double x, double z,
                                  double margin);

// Signed distance to the void of the entrance, the union of its tube (a capsule from top to
// bottom) and of its chamber (a ball around bottom). Negative inside.
[[nodiscard]] float entranceVoidAt(const CaveEntrance& entrance, double x, double y, double z);

} // namespace terrain
