#pragma once

#include "unreal_frame.hpp"

#include "terrain/streaming/horizon_grids.hpp"
#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/world_size.hpp"

namespace terrain::unreal {

// Layout of the far horizon in Unreal (phase 13): which cells each level of the clipmap draws,
// where each level stands, and the drop of the curvature of the planet.
//
// The cells of a level are addressed from the low corner of its window: cell (i, k) lies between
// samples (i, k) and (i + 1, k + 1), with i along x and k along z, both in [0, K). Each level
// draws its body, the cells of its window outside a square around its center:
//   - for levels 1 and up, the square of 2 (K / 4 + 1) cells that holds the window of the finer
//     level wherever it stands (one cell around it at most, finerWindowCells), so the body keeps
//     the same triangles when the windows move; its trim fills the cells of that square left
//     by the finer window, a frame of 1 or 2 cells;
//   - for level 0, the square under the tiles (TileHole).
// Together the levels cover the window of the coarsest level once, outside the tile hole.

// Cells [i0, i1) x [k0, k1) of a level window.
struct CellRect {
    int i0 = 0;
    int k0 = 0;
    int i1 = 0;
    int k1 = 0;

    [[nodiscard]] bool empty() const { return i1 <= i0 || k1 <= k0; }
    [[nodiscard]] bool contains(int i, int k) const {
        return i >= i0 && i < i1 && k >= k0 && k < k1;
    }
    [[nodiscard]] bool contains(const CellRect& other) const {
        return other.i0 >= i0 && other.i1 <= i1 && other.k0 >= k0 && other.k1 <= k1;
    }

    friend bool operator==(const CellRect&, const CellRect&) = default;
};

// The square of 2 half cells around the center of a window of cells x cells (cells even).
[[nodiscard]] CellRect centeredSquare(int cells, int half);

// The square a level body leaves out: the one that holds every position of the finer window,
// 2 (K / 4 + 1) cells, for levels 1 and up; the tile hole for level 0.
[[nodiscard]] CellRect bodyHole(const HorizonParams& params, int level, int tileHoleCells);

// The cells of the window of the finer level (level - 1) in the window of a level, both placed
// (the levels of one clipmap): K / 2 cells, at most one cell off the center of the square of
// bodyHole. Throws std::logic_error for windows that are not nested so.
[[nodiscard]] CellRect finerWindowCells(const HorizonLevel& level, const HorizonLevel& finer);

// The hole of level 0 under the tiles, a square around its center. It holds every tile of LOD 0
// to kMaxLod - 1 (fineTilesReach): they show caves, cliffs and overhangs the horizon must never
// hide. Around it, the tiles of LOD 3 cover level 0 on a band of overlapMeters, kTileOverlapMeters
// when they reach that far (tileCoverage): the horizon lies there below the tiles, and the tiles
// streamed late at the edge of their area leave no gap while the hole moves.
struct TileHole {
    int halfCells = 0;          // the hole is centeredSquare(K, halfCells) of level 0
    double overlapMeters = 0.0; // the guaranteed band of tiles around the hole, outward

    friend bool operator==(const TileHole&, const TileHole&) = default;
};

inline constexpr double kTileOverlapMeters = 128.0; // one tile of LOD 3
inline constexpr double kMinTileOverlapMeters = 64.0;

// Chebyshev distance from the player within which the tiles cover every point: the chunks within
// viewDistance of its chunk, meters.
[[nodiscard]] double tileCoverage(const LodParams& lod);

// Chebyshev distance from the player beyond which no tile is finer than kMaxLod, meters: the
// coarsest tiles split are those whose nearest chunk lies within splitDistance[kMaxLod - 1] - 1
// chunks of the chunk of the player.
[[nodiscard]] double fineTilesReach(const LodParams& lod);

// Throws std::invalid_argument unless the tiles end inside level 0 (validate(params, lod)) and
// the overlap reaches kMinTileOverlapMeters (a view distance large enough beyond the fine
// tiles). The center of level 0 stands within one pitch of the player.
[[nodiscard]] TileHole tileHole(const HorizonParams& params, const LodParams& lod);

// Unreal position of the low corner of the window of a level at its image nearest to the
// observer (unwrapped core position), at the altitude 0: the origin of its mesh.
[[nodiscard]] UnrealPoint levelOrigin(const HorizonLevel& level, const UnrealFrame& frame,
                                      const CorePoint& observer);

// Radius of the planet the wrapped world stands for: its circumference along x is the length of
// the equator, about 9,554 km by default.
[[nodiscard]] double planetRadiusMeters(const WorldSize& world);

// How far the ground at this horizontal distance falls below the plane of the observer on a
// planet of this radius: d^2 / 2R (the material of the horizon moves its vertices so).
[[nodiscard]] double curvatureDropMeters(double distance, double radius);

// The largest horizontal distance from the observer to the window of a level: its farthest
// corner, its center being within one pitch of the observer.
[[nodiscard]] double levelReachMeters(const HorizonParams& params, int level);

// The crest ratio (HorizonParams::crestRatio) of a setting of the editor: 0 below 2 (point
// samples), else the largest of 2, 4 and 8 not above it.
[[nodiscard]] int crestRatioSetting(int requested);

} // namespace terrain::unreal
