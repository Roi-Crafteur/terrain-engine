#pragma once

#include <array>
#include <cstdint>

namespace terrain {

// The rock of the streamed world in three dimensions (phase 12A). The relief is a height per
// column (Heightfield::heightAt), whose walls keep a slope; where a generator of walls lays one (the
// walls of the mountains, the slots of the falls and the gorges, the escarpments), it describes to
// the columns near it a face: a riser of the relief turned vertical, whose top may jut over the air
// (an overhang) and whose foot may recede into the rock (an undercut, a niche). The faces are
// measured in meters across them, on the smooth ground the generator lays the riser on, so that the
// rock in front of a face or behind it is found without any height of the neighbors. Everywhere
// else the density of the rock is y - heightAt(x, z), bit for bit.

// A face seen from a column: the rock lies on the side of negative distances.
struct RockFace {
    float foot = 0.0f; // heights of its foot and its top in the column, meters
    float top = 0.0f;
    // Horizontal meters from the column to the face: positive in front of it (the side of the air),
    // negative behind it (the side of the rock).
    float distance = 0.0f;
    // The top juts over the air by cap meters, from capDepth meters below it up to it; the foot
    // recedes into the rock by undercut meters, up to undercutHeight meters above it.
    float cap = 0.0f;
    float capDepth = 0.0f;
    float undercut = 0.0f;
    float undercutHeight = 0.0f;
    // Where the wall is faint, the face leans back: from run / 2 meters toward the air at its foot
    // to run / 2 meters into the rock at its top (the slope of the riser of the relief), by
    // 1 - vertical of it; vertical 1 is a vertical face.
    float run = 0.0f;
    float vertical = 1.0f;
    // A face of the valley of a river (its slot, its gorge, the face of a fall): it shapes the air
    // the river carves; otherwise a face of the relief before the rivers.
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

    // Appends a face; past kMaxRockFaces, the face nearest to the column is kept.
    void add(const RockFace& face);

    friend bool operator==(const RockColumn&, const RockColumn&) = default;
};

// The rock a level of detail draws: the caps and the undercuts narrower than leastFeature meters
// are left out (the cells of a coarse tile cannot hold them).
struct RockDetail {
    float leastFeature = 0.0f;

    friend bool operator==(const RockDetail&, const RockDetail&) = default;
};

// The detail of the tiles of a LOD (lod.hpp): everything up to LOD 2, the features of half a cell
// at least on LOD 3.
[[nodiscard]] RockDetail rockDetailForLod(int lod);

// Density at height y of a column, meters, negative in the rock: y - ground bit for bit without a
// face; with faces, the horizontal distance to a face within its heights, the vertical one
// elsewhere, the air the rivers carve in the relief taken out of it (and the levees they fill
// added).
[[nodiscard]] float rockDensity(const RockColumn& column, double y, const RockDetail& detail);

// The heights between which the surfaces of the rock of a column lie.
struct RockBounds {
    float low = 0.0f;
    float high = 0.0f;
};
[[nodiscard]] RockBounds rockBounds(const RockColumn& column);

// The share of bare rock, in [0, 1], of the surface of a column at height y: 1 on its faces, under
// their caps and in their undercuts, 0 elsewhere (the material of the ground decides there).
[[nodiscard]] float rockShare(const RockColumn& column, double y);

} // namespace terrain
