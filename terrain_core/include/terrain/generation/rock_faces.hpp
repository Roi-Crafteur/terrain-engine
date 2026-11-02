#pragma once

#include <array>
#include <cstdint>

namespace terrain {

// The rock of the streamed world in three dimensions (phase 12A). The relief is a height per
// column (Heightfield::heightAt), whose walls keep a slope; where a generator of walls lays one
// (the walls of the mountains, the slots of the falls and the gorges, the escarpments), it
// describes to the columns of its riser a face: the relief climbs from the foot of the riser to
// its top over its run, and the face turns that climb into a step, a vertical face at the middle
// of the run whose top may jut over the air (an overhang) and whose foot may recede into the rock
// (an undercut, a niche). The heights of the foot and the top are those of the relief at the two
// edges of the run, so the step meets the relief there: within the run, the rock stands up to the
// top behind the face and to the foot in front of it, two ledges whose heights follow the relief
// along the face. (Within the run, the relief steeper than its line would add its own detail to
// the ledges by as much as its slope times any error on the place of its riser: a lip toothed
// from column to column.) Everywhere else the density of the rock is y - heightAt(x, z), bit for
// bit.

// The face of a riser seen from a column of its run.
struct RockFace {
    float foot = 0.0f; // heights of the relief at the foot and at the top of the run, meters
    float top = 0.0f;
    // Horizontal meters from the middle of the run to the column, positive toward the foot (the
    // side of the air), within run / 2 of it.
    float distance = 0.0f;
    float run = 0.0f;
    // The top juts over the air by cap meters, from capDepth meters below it up to it; the foot
    // recedes into the rock by undercut meters, up to undercutHeight meters above it. Both stay
    // within the run.
    float cap = 0.0f;
    float capDepth = 0.0f;
    float undercut = 0.0f;
    float undercutHeight = 0.0f;
    // The grain of its rock: the face stands out or recedes by up to grain meters, by beds whose
    // heights shift by grainShift meters from column to column.
    float grain = 0.0f;
    float grainShift = 0.0f;
    // A vertical joint of the rock crossing the face: it recedes by joint meters at the column, in
    // a groove jointWidth meters wide, over stretches of its height drawn by jointSeed (in [0, 1),
    // the same all along the joint).
    float joint = 0.0f;
    float jointWidth = 0.0f;
    float jointSeed = 0.0f;
    // How far the step replaces the climb of the relief, in [0, 1] (0: the relief as it is).
    float weight = 1.0f;
    // The horizontal direction of the air from the rock (a unit vector, x and z), along which the
    // run lies.
    float airX = 0.0f;
    float airZ = 0.0f;
    // A face of the valley of a river (its slot, its gorge, the face of a fall): its step shapes
    // the floor the river carves; otherwise a face of the relief before the rivers.
    bool carved = false;

    friend bool operator==(const RockFace&, const RockFace&) = default;
};

inline constexpr int kMaxRockFaces = 4;

// What the rock of a column depends on, apart from y.
struct RockColumn {
    float ground = 0.0f;   // heightAt: the relief with the valleys of the rivers
    float uncarved = 0.0f; // the relief before the rivers carve or fill it
    std::uint8_t faceCount = 0;
    std::array<RockFace, kMaxRockFaces> faces{};

    // Appends a face; past kMaxRockFaces, the face of the greatest weight is kept.
    void add(const RockFace& face);

    friend bool operator==(const RockColumn&, const RockColumn&) = default;
};

// The rock a level of detail draws on cells of cell meters: the faces whose run spans less than
// kRunCells cells fade out, and the caps and the undercuts narrower than 3 cells (the columns
// would alternate in and out of them along the face, a regular row of teeth), and the grain finer
// than its cells.
struct RockDetail {
    float cell = 1.0f;

    friend bool operator==(const RockDetail&, const RockDetail&) = default;
};
inline constexpr float kRunCells = 4.0f;

// The detail of the tiles of a LOD (lod.hpp): cells of 2^lod meters.
[[nodiscard]] RockDetail rockDetailForLod(int lod);

// Density at height y of a column, meters, negative in the rock: y - ground bit for bit without a
// face the detail draws; with faces, the relief and the steps of the faces of its runs blended by
// their weights, the air the rivers carve in it taken out (their floor stepped by the faces of
// their valley) and the levees they fill added.
[[nodiscard]] float rockDensity(const RockColumn& column, double y, const RockDetail& detail);

// How far a detail draws a face: its weight, faded out over runs of kRunCells to 1.5 times that
// many cells. A column whose faces a detail does not draw is the relief alone there.
[[nodiscard]] float drawnWeight(const RockFace& face, const RockDetail& detail);

// Sets the heights of the foot and the top of a face to those of the relief at the edges of its
// run, its overhangs scaled with its height (they were measured on a face as high as top - foot).
// The weight fades out on faces lower than kLeastRockFace to twice that.
void anchorFace(RockFace& face, float foot, float top);
inline constexpr float kLeastRockFace = 3.0f;

// The heights between which the surfaces of the rock of a column lie.
struct RockBounds {
    float low = 0.0f;
    float high = 0.0f;
};
[[nodiscard]] RockBounds rockBounds(const RockColumn& column);

// The share of bare rock, in [0, 1], of the surface of a column at height y: the weight the
// detail draws of its faces on them, under their caps and in their undercuts, 0 elsewhere (the
// material of the ground decides there).
[[nodiscard]] float rockShare(const RockColumn& column, double y, const RockDetail& detail);

} // namespace terrain
