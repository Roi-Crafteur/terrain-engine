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
//   its foot (an undercut 1 to 3 m deep and high, where the noise of its overhangs opens it: never
//   a regular row).
// - Where two of these kinds share the coast, the face fades out between them: it never jumps
//   along the coast from one place to the other.
// Its heights are those of the ground of the coast at the ends of its run, at the column (relative
// to the ground there, to anchor on the relief: anchorFace): the step meets the relief at both
// ends. Its weight is the share of the rocky coasts, the cliffs and the calanques among the kinds
// of coast; the cliffs and the calanques jut out at their top where the overhangs want it. Its
// grain, its beds and its joints are those of the rock of the walls (wall_faces::Grain). The
// Heightfield takes it out near the rivers (Hydrology::clearance: no rock over their water).

#include "coast_kinds.hpp"
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
};

// The face of the coast at a point of the band of the kinds of coast weights over the geography
// g, the inlets and stacks there inlets (coast_shape::shapeAt): none (faced false) out of a run.
[[nodiscard]] Report faceAt(const GeographyParams& p, const coast_shape::Noises& noises,
                            const wall_faces::Grain& grain, const Point& point,
                            const GeographySample& g, const coast_kinds::Weights& weights,
                            const coast_shape::Inlets& inlets);

// Whether the sea at a point lies before the face of a calanque, within its run (bare rock there:
// the tiles of the coarser details draw the face over the material of the ground before it).
[[nodiscard]] bool plungeRock(const coast_shape::Noises& noises, const Point& point,
                              const GeographySample& g, const coast_kinds::Weights& weights);

// The height of the surface of a column of relief relief seen from above, with its face anchored
// (absolute heights) as a detail draws it: the ledge of its top behind the face, that of its foot
// in front of it (between them over a cell of the detail across the face: its line, not a
// staircase of samples), both leaned to the relief by the weight the detail draws; the
// relief without a face drawn, and in front of it under the sea (the floor the horizon colors the
// sea by: no sea painted up the wall). The horizon shows the face where the tiles leave it off:
// the same step at 512 m, no cliff moving back.
[[nodiscard]] float seenHeight(const RockFace& face, float relief, const RockDetail& detail);

} // namespace terrain::coast_faces
