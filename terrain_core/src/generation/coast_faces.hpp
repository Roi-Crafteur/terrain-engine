#pragma once

// The faces of the coasts in three dimensions (agent 33, on the rock of phase 12A: rock_faces.hpp),
// internal to terrain_core. Where the rocky coasts, the cliffs and the calanques rise as a face
// (coast_shape, coast_profile), the columns of a run around it see a face standing vertical in
// it, as the faces of the walls of the mountains (wall_faces.hpp):
// Where it stands (coast_profile::runOf):
// - The calanques plunge: their face stands at the shore itself, its run reaching as far out under
//   the sea as up to the top of the face, the sea alone in front of it (no strip of ground at the
//   water before a calanque).
// - The cliffs and the rocky coasts keep their rock platform: their face stands at its end, or
//   further up the slope when the platform leaves no room, the platform before it, its notch at
//   its foot (an undercut 3 to 4 m deep and 6 to 8 m high, 30 % of the face at most, where the
//   noise of its overhangs opens it: never a regular row). The tiles of LOD 0 draw it whole, those
//   of LOD 1 a little of it, the coarser ones none (they draw no overhang finer than their cells:
//   rock_faces.hpp), nor does the horizon.
// - Where two of these kinds share the coast, the face fades out between them: it never jumps
//   along the coast from one place to the other.
// - The lower walls of the fjords and the rocks of the reefs (the stacks all around) rise from the
//   sea level, the floor under the sea before them as the relief has it: the columns of the sea
//   do not hold their faces, which never dig under it (no air shut in the rock there).
// - Near a rock of the reefs, the faces of the shore fade out, and the faces of two rocks fade
//   out where they meet: no face is ever cut off from one column to the next.
// Its heights are those of the ground of the coast at the ends of its run, at the column (relative
// to the ground there, to anchor on the relief: anchorFace): the step meets the relief at both
// ends. Its weight is the share of the rocky coasts, the cliffs and the calanques among the kinds
// of coast; the cliffs and the calanques jut out at their top where the overhangs want it (a cap
// of 10 % of the face, 4 m at most, drawn as the notches). Its
// grain, its beds and its joints are those of the rock of the walls (wall_faces::Grain). The
// Heightfield takes it out near the rivers (Hydrology::clearance: no rock over their water).

#include "coast_kinds.hpp"
#include "coast_profile.hpp"
#include "coast_shape.hpp"
#include "wall_faces.hpp"

#include "terrain/generation/geography.hpp"
#include "terrain/generation/geography_params.hpp"
#include "terrain/generation/rock_faces.hpp"

namespace terrain::coast_faces {

// A point of the band of the coasts, as the Heightfield reads it (coastPointAt).
struct Point {
    double x = 0.0; // canonical
    double z = 0.0;
    double distance = 0.0; // to the shore of the world, meters, signed as the coast signal
    double gradient = 1.0; // of the coast signal of the geography, per meter
    double airX = 1.0;     // toward the sea, a unit vector
    double airZ = 0.0;
};

// What a point reports of its face (Heightfield::Memos::coastFace): whether one lies at it, and
// the face, its heights relative to the ground of the coast at the point.
struct Report {
    bool faced = false;
    RockFace face;
    // The face rises from the sea (the calanques, the lower walls of the fjords, the rocks of the
    // reefs): the sea lies before it, no platform.
    bool plunges = false;
    // Its distance and its run are measured along the distance to the shore of the point
    // (Point::distance), which the inlets bend (coast_inlets::distanceOf: a fraction of a meter
    // per meter by the walls of a calanque or a fjord), not in meters: the Heightfield turns them
    // into meters (Heightfield::distancePerMeter), the overhangs, the grain and the joints of the
    // face being in meters.
    bool alongShore = false;
    // Only within front meters before the plane of the face (in meters, 0: all of its run).
    float front = 0.0f;
    // A rock of the reefs (a block or a stack): the spire of rock over the point (rock_faces.hpp,
    // coast_reefs::spireOf), no face (radius 0: none).
    RockSpire spire;
    // A point of the land without a face, within the ramp of the shore of a level of the horizon
    // (Heightfield::Memos::seenRamp): its distance to the shore, meters; -1 elsewhere.
    float shore = -1.0f;
};

// The face of the coast at a point of the band of the kinds of coast weights over the geography
// g, the inlets and stacks there inlets, the noises of its shore shore (coast_shape::shapeAt):
// none (faced false) out of a run.
[[nodiscard]] Report faceAt(const GeographyParams& p, const wall_faces::Grain& grain,
                            const Point& point, const GeographySample& g,
                            const coast_kinds::Weights& weights, const coast_shape::Inlets& inlets,
                            const coast_profile::Shore& shore);

// Whether the sea at a point lies before the face of a calanque, within its run (bare rock there:
// the tiles of the coarser details draw the face over the material of the ground before it).
[[nodiscard]] bool plungeRock(const Point& point, const GeographySample& g,
                              const coast_kinds::Weights& weights,
                              const coast_profile::Shore& shore);

// What a level of the horizon of pitch meters shows of a column of relief relief with its face
// anchored (absolute heights): the ledge of its top behind the face, that of its foot in front of
// it, leaned to the relief by the weight the tiles of LOD 3 draw (kSeenCell), with a ramp between
// them as wide as kSeenRamp pitches of the level on either side of the face (a half of its run at
// most): its samples, its slopes and its detail normals then follow the line of the face, which a
// step narrower than its lattice would cut into a staircase of light and dark triangles. A face
// that plunges rises from the sea level at the shore over twice that width (the sea before it
// keeps the floor of its relief, by which the horizon colors it): the samples of the land at the
// water stand at the sea level, so that the sea the horizon draws between them and the sea does
// not climb the wall; its top stands a pitch further from the sea than in the tiles at most. The
// relief without a face drawn. rock: the ramp and a pitch on either side of it, bare rock.
struct Seen {
    float height = 0.0f;
    bool rock = false;
};
inline constexpr float kSeenCell = 8.0f;
inline constexpr double kSeenRamp = 1.0;
[[nodiscard]] Seen seenOf(const RockFace& face, bool plunges, float relief, double pitch);

// Every gentle shore of a level of the horizon of pitch meters rises from the sea level over the
// width of its ramp (twice kSeenRamp pitches, as a face that plunges): the samples of the land at
// the water stand at the sea level, so that the sea the material of the horizon paints between
// them and the sea samples does not climb the slope there either (a bank without a face: the
// walls of a fjord too gentle for one, agent 40 for the rest). A bank as steep as a wall over that
// width keeps its height (the sea at its foot is drawn as rock). The land shore meters from the
// water, its relief relief: what that level shows of it.
[[nodiscard]] double shoreRampOf(double pitch);
[[nodiscard]] float seenShore(float relief, float shore, double pitch);

} // namespace terrain::coast_faces
