#include "horizon_layout.hpp"

#include "terrain/chunk_coord.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>

namespace terrain::unreal {

namespace {

// Cells from the low corner of the window of level to the one of finer, along an axis, given
// the low corners of both windows at the same image (meters, multiples of their pitches).
int cellsBetween(double finerOrigin, double levelOrigin, int pitch) {
    const double cells = (finerOrigin - levelOrigin) / pitch; // powers of two: exact
    if (!(std::abs(cells) < 1.0e9) || cells != std::floor(cells)) {
        throw std::logic_error("finerWindowCells: the windows are not on the same lattice");
    }
    return static_cast<int>(cells);
}

} // namespace

CellRect centeredSquare(int cells, int half) {
    const int center = cells / 2;
    return {center - half, center - half, center + half, center + half};
}

CellRect bodyHole(const HorizonParams& params, int level, int tileHoleCells) {
    const int cells = params.cellsPerSide;
    return centeredSquare(cells, level == 0 ? tileHoleCells : cells / 4 + 1);
}

CellRect finerWindowCells(const HorizonLevel& level, const HorizonLevel& finer) {
    if (!level.placed() || !finer.placed() || finer.level() + 1 != level.level()) {
        throw std::logic_error("finerWindowCells: two placed levels, the finer one first");
    }
    // Any observer gives both windows at the same image: the center of the coarser one.
    const double x = samplePosition(wrapIndex(level.center().x, level.counts().x), level.pitch());
    const double z = samplePosition(wrapIndex(level.center().z, level.counts().z), level.pitch());
    const int i0 = cellsBetween(finer.originX(x), level.originX(x), level.pitch());
    const int k0 = cellsBetween(finer.originZ(z), level.originZ(z), level.pitch());
    const int cells = level.side() - 1;
    const CellRect window{i0, k0, i0 + cells / 2, k0 + cells / 2};
    if (!centeredSquare(cells, cells / 4 + 1).contains(window)) {
        throw std::logic_error("finerWindowCells: the window of the finer level is off center");
    }
    return window;
}

double tileCoverage(const LodParams& lod) {
    return lod.viewDistance * kChunkSizeMeters;
}

double fineTilesReach(const LodParams& lod) {
    const int split = lod.splitDistance[static_cast<std::size_t>(kMaxLod - 1)];
    return (split + tileChunks(kMaxLod) - 1) * kChunkSizeMeters;
}

TileHole tileHole(const HorizonParams& params, const LodParams& lod) {
    validate(params, lod);
    const double pitch = levelPitch(params, 0);
    // Around the center of level 0, which stands within one pitch of the player.
    const double covered = tileCoverage(lod) - pitch;
    const double fine = fineTilesReach(lod) + pitch;
    const double half = std::max(fine, covered - kTileOverlapMeters);
    const int cells = static_cast<int>(std::ceil(half / pitch));
    const double overlap = covered - cells * pitch;
    if (overlap < kMinTileOverlapMeters) {
        throw std::invalid_argument(
            "the tiles cover level 0 on " + std::to_string(static_cast<int>(overlap)) +
            " m around the tiles of LOD 0 to 2, less than " +
            std::to_string(static_cast<int>(kMinTileOverlapMeters)) +
            " m: raise the view distance");
    }
    return {cells, overlap};
}

UnrealPoint levelOrigin(const HorizonLevel& level, const UnrealFrame& frame,
                        const CorePoint& observer) {
    return toUnreal(frame, {level.originX(observer.x), 0.0, level.originZ(observer.z)});
}

double planetRadiusMeters(const WorldSize& world) {
    return world.x / (2.0 * std::numbers::pi);
}

double curvatureDropMeters(double distance, double radius) {
    return distance * distance / (2.0 * radius);
}

double levelReachMeters(const HorizonParams& params, int level) {
    const double half = (params.cellsPerSide / 2 + 1) * static_cast<double>(levelPitch(params, level));
    return half * std::numbers::sqrt2;
}

} // namespace terrain::unreal
