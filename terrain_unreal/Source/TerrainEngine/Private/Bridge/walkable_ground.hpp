#pragma once

#include "terrain/generation/heightfield.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace terrain::unreal {

// Where the pawn of the game is put down when the tools give it back (agent 36,
// ATerrainWorld::ReturnToGamePawn, Terrain.Pawn; agent 35 for a click on the map): the walkable
// ground nearest to the column under the view, never a slope it would slide down (73 degrees
// under the wall of the K2) nor water. Two steps:
// - the candidates, read on the heightmap of the core (walkableCandidates): dry columns whose
//   slope is under the walkable angle of the pawn, nearest first;
// - each candidate checked on the real collision of the tiles (checkStand): rays down at its
//   center and around the capsule. The tiles are meshed from the 3D SDF (cliffs, overhangs):
//   their ground may be steeper than the heightmap, or away from it.
// When the 3D rock of task 12A is on main, the candidates will have to read the real column.

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
using GroundSample = std::function<HeightSample(double x, double z)>;

// The steepest slope of the ground at (x, z): the largest of the four between it and the points
// probe meters east, west, south and north (a cautious measure: a step of a wall counts).
[[nodiscard]] double groundSlope(double x, double z, double probe, const GroundHeight& height);

// The slope a pawn walks up: tan(walkable angle - margin), the angles in degrees, 0 when the
// margin takes it all. The walkable angle of a CharacterMovement is 44.765 degrees by default.
[[nodiscard]] double walkableSlope(double walkableAngleDegrees, double marginDegrees);
// The same limit on the normal of a floor: its up component cos(walkable angle - margin).
[[nodiscard]] double walkableNormalUp(double walkableAngleDegrees, double marginDegrees);

// Why no candidate was found: none of the columns of the search is dry (the view over a sea or
// a lake: agent 35 then poses the flying view 1.8 m above the water of the core), or the dry
// ones are all too steep.
enum class WalkableReason : std::uint8_t { Found, NoDryGround, TooSteep, BadSearch };
[[nodiscard]] const char* walkableReasonText(WalkableReason reason);

struct WalkableCandidates {
    std::vector<WalkableSpot> spots; // nearest ring first, the gentlest first on a ring
    WalkableReason reason = WalkableReason::BadSearch;
};

// The dry points whose slope is at most search.maxSlope: the column itself, then the rings
// outward, each walked from the north clockwise; on a ring the gentlest first (the first of equal
// slopes); at most maxCount, the rings stopped once that many are found. Deterministic.
[[nodiscard]] WalkableCandidates walkableCandidates(double x, double z,
                                                    const WalkableSearch& search,
                                                    const GroundHeight& height,
                                                    const GroundWet& wet, std::size_t maxCount);

// The first candidate, or nullopt.
[[nodiscard]] std::optional<WalkableSpot> nearestWalkable(double x, double z,
                                                          const WalkableSearch& search,
                                                          const GroundHeight& height,
                                                          const GroundWet& wet);

// The highest water surface of the core at (x, z) and at 8 points radius meters around it, or
// nullopt when all of them are dry (columnHasWater).
[[nodiscard]] std::optional<double> waterSurfaceAround(double x, double z, double radius,
                                                       const GroundSample& sample);

// A ray down onto the collision of the tiles: whether it hit, the altitude of the hit (meters of
// the core) and the up component of the normal there.
struct FloorProbe {
    bool hit = false;
    double altitude = 0.0;
    double normalUp = 0.0;
};

struct StandLimits {
    double minNormalUp = 0.0;     // walkableNormalUp of the pawn
    double maxStep = 0.45;        // meters between the hits (MaxStepHeight of a CharacterMovement)
    double maxOffHeightmap = 2.0; // meters between the hit of the center and the heightmap
};

enum class StandRefusal : std::uint8_t {
    None,
    NoCollision,  // a ray hit nothing (the tiles not loaded there yet?)
    TooSteep,     // a normal steeper than the walkable angle less the margin
    Step,         // the hits further apart than a step
    OffHeightmap, // the collision far from the heightmap: a cliff, an overhang, a hollow
    UnderWater,   // the hit under the water surface of the core around
};
[[nodiscard]] const char* standRefusalText(StandRefusal refusal);

struct StandCheck {
    StandRefusal refusal = StandRefusal::NoCollision;
    double altitude = 0.0; // the highest hit: where the capsule goes down
};

// Whether the pawn stands at a candidate: probes[0] the ray of its center, the others around the
// capsule; heightmapGround the ground of the heightmap there; waterSurface waterSurfaceAround.
[[nodiscard]] StandCheck checkStand(std::span<const FloorProbe> probes, double heightmapGround,
                                    std::optional<double> waterSurface, const StandLimits& limits);

} // namespace terrain::unreal
