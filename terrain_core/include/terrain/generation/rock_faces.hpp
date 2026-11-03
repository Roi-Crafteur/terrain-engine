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
    // The edges of its step rounded over roundTenths tenths of a meter at most (0: the rounding of
    // the walls, or of the carved faces); the faces of the coasts are sharper (agent 33: no
    // cushion at their top). A byte by carved: the size of a face stays (the column cache).
    std::uint8_t roundTenths = 0;

    friend bool operator==(const RockFace&, const RockFace&) = default;
};

inline constexpr int kMaxRockFaces = 4;

// A spire of rock standing over a column (the stacks of the coasts, agent 33: coast.hpp), a solid
// of its own in three dimensions (a distance field, rock_spires.hpp) added to the relief: the
// column lies (dx, dz) meters from its axis; its shape (a pillar, a needle, a stack split in two,
// an arch, their benches, their notch, their cracks) is drawn from seed and its size, its radius
// at the sea level and its height above it (0: none).
struct RockSpire {
    float dx = 0.0f;
    float dz = 0.0f;
    float radius = 0.0f;
    float height = 0.0f;
    std::uint32_t seed = 0;

    friend bool operator==(const RockSpire&, const RockSpire&) = default;
};

// What the rock of a column depends on, apart from y.
struct RockColumn {
    float ground = 0.0f;   // heightAt: the relief with the valleys of the rivers
    float uncarved = 0.0f; // the relief before the rivers carve or fill it
    std::uint8_t faceCount = 0;
    std::array<RockFace, kMaxRockFaces> faces{};
    RockSpire spire;

    // Whether it holds a face or a spire (its rock in three dimensions).
    [[nodiscard]] bool hasRock() const { return faceCount > 0 || spire.radius > 0.0f; }

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

// The density of a spire alone at (dx, dz) meters from its axis, height y, as the detail draws
// it (rockDensity is the least of it and of the relief with the faces).
[[nodiscard]] float spireDensity(const RockSpire& spire, double dx, double dz, double y,
                                 const RockDetail& detail);

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

// The highest the rock of a column stands (rockDensity), above which it is air at every detail:
// its ground, the tops of the faces of the relief, and the relief before the rivers only as high
// as the valley of a river with its carved faces rises (their step is taken out of that relief).
// Not the relief before the rivers alone: a valley carved 1.6 km deep holds no rock up there.
[[nodiscard]] float rockCeiling(const RockColumn& column);

// The share of bare rock, in [0, 1], of the surface of a column at height y: the weight the
// detail draws of its faces on them, under their caps and in their undercuts, 0 elsewhere (the
// material of the ground decides there).
[[nodiscard]] float rockShare(const RockColumn& column, double y, const RockDetail& detail);

// The bands of the rock of a column at height y for the materials (agent 33): what the cells of
// the coarse tiles and of the horizon do not draw of its faces and its spire, each as a height
// relative to y (linear in it within a bed: a triangle interpolates them between its vertices,
// and the materials draw them at the same heights at every LOD).
struct RockBands {
    // Its beds: the coordinate of y among them (bed k from k to k + 1, k counted from a base
    // that changes only every few hundred meters of height, the same for every column there),
    // and how much they show, in [0, 1] (0: none).
    float strata = 0.0f;
    float layered = 0.0f;
    // Its notch (the undercut at the foot of a face, the notch of a spire at the sea): y over its
    // floor, inside it from 0 to notchHeight (0: none).
    float notch = 0.0f;
    float notchHeight = 0.0f;
    // Its cap (the cornice at the top of a face): y over its underside, inside it from 0 to
    // capHeight (0: none).
    float cap = 0.0f;
    float capHeight = 0.0f;

    friend bool operator==(const RockBands&, const RockBands&) = default;
};

// The bands of a column at height y: those of its spire where it holds one, else those of its
// face of the greatest weight, none without either. Its beds show where its rock has a grain (the
// beds of a face: grainAt; a spire: its beds), its notch and its cap as deep as a few cells at
// least would draw them (kBandLeast meters).
[[nodiscard]] RockBands rockBands(const RockColumn& column, double y);
inline constexpr float kBandLeast = 0.5f;

} // namespace terrain
