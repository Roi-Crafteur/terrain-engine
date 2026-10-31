#pragma once

#include <functional>
#include <optional>

namespace terrain::unreal {

// Where the pawn of the game is put down when the tools give it back (agent 36,
// ATerrainWorld::ReturnToGamePawn, Terrain.Pawn): the walkable ground nearest to the column under
// the view, never a slope it would slide down (73 degrees under the wall of the K2) nor water.
// It reads the heightmap of the core, as the ground under the view does: no overhang, no wall of
// the 3D rock (when the 3D rock of task 12A is on main, the real column will have to be read).

// The search around the column: rings ringStep meters apart up to radius, each walked along in
// steps of about ringStep meters.
struct WalkableSearch {
    double radius = 50.0;
    double ringStep = 2.0;
    double maxSlope = 0.0; // rise over run: walkableSlope of the pawn
    double probe = 1.0;    // meters: the slope is measured that far around a point
};

struct WalkableSpot {
    double x = 0.0;
    double z = 0.0;
    double ground = 0.0;   // the height of the heightmap there, meters
    double slope = 0.0;    // its slope, rise over run (groundSlope)
    double distance = 0.0; // from the column, meters: the radius of its ring
};

using GroundHeight = std::function<double(double x, double z)>;
using GroundWet = std::function<bool(double x, double z)>;

// The steepest slope of the ground at (x, z): the largest of the four between it and the points
// probe meters east, west, south and north (a cautious measure: a step of a wall counts).
[[nodiscard]] double groundSlope(double x, double z, double probe, const GroundHeight& height);

// The slope a pawn walks up: tan(walkable angle - margin), the angles in degrees, 0 when the
// margin takes it all. The walkable angle of a CharacterMovement is 44.765 degrees by default.
[[nodiscard]] double walkableSlope(double walkableAngleDegrees, double marginDegrees);

// The walkable spot nearest to (x, z): a dry point whose slope is at most search.maxSlope. The
// column itself first, then the rings outward, each walked from the north clockwise; on the
// nearest ring that has one, the gentlest (the first of equal slopes). nullopt when there is none
// within search.radius. Deterministic: the same heights give the same spot.
[[nodiscard]] std::optional<WalkableSpot> nearestWalkable(double x, double z,
                                                          const WalkableSearch& search,
                                                          const GroundHeight& height,
                                                          const GroundWet& wet);

} // namespace terrain::unreal
