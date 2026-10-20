#pragma once

#include "terrain/det_math.hpp" // also refuses fast math in every unit including it

#include <cstdint>

namespace terrain {

// Every side of the world is a multiple of this, in meters: a multiple of the largest tile (and
// of two of them, so that the tiles of the coarsest LOD never straddle the edge of the
// canonical range) and of the grids of the world format.
inline constexpr double kWorldAlignmentMeters = 65536.0;
inline constexpr double kMaxWorldMeters = 1073741824.0; // 2^30 m: chunk and block coordinates fit

// The world wraps around: a flat torus that simulates a planet, whose curvature is left to the
// renderer. x wraps east to west with period x, z north to south with period z, and the north
// and south edges (z = -z / 2, the same line as z / 2) are both polar, so that the passage is
// seamless. The canonical range is [-x / 2, x / 2) x [-z / 2, z / 2): the origin is the middle
// of the map, on the equator.
//
// Every layer of the generation is periodic on it: its public sampling functions bring their
// coordinates back into the canonical range first (wrapCoordinate, exact), so that the same seed
// gives the same result at (x, z) and at (x + k sizeX, z + l sizeZ) bit for bit whenever those
// coordinates are exactly representable: every position the engine samples (integers and
// multiples of 2^-k meters). Every lattice and grid falls exactly on the period (PeriodicCells).
struct WorldSize {
    double x = 60'030'976.0; // 916 x 65,536 m, about 60,000 km
    double z = 34'996'224.0; // 534 x 65,536 m, about 35,000 km

    friend bool operator==(const WorldSize&, const WorldSize&) = default;
};

// Throws std::invalid_argument unless both sides are finite multiples of kWorldAlignmentMeters,
// at most kMaxWorldMeters.
void validate(const WorldSize& world);

// v brought back into [-period / 2, period / 2): v - k * period for an integer k, computed
// without rounding (period is a multiple of 2^16), so wrapCoordinate(v + period) equals
// wrapCoordinate(v) whenever v + period is exactly representable. NaN for a non-finite v.
[[nodiscard]] double wrapCoordinate(double v, double period);

// The image v + k * period nearest to reference (within half a period).
[[nodiscard]] double nearestImage(double v, double reference, double period);

// Index brought back into [-(count / 2), count - count / 2), the canonical range of a periodic
// grid of count cells (integer division): i - k * count.
[[nodiscard]] std::int64_t wrapIndex(std::int64_t i, std::int64_t count);

// A grid along one axis that falls exactly on a period: count = round(period / nominal) cells,
// at least 1, of size period / count (exactly the nominal size when it divides the period).
//
// Cells are addressed by unwrapped indices, around the coordinate they are queried from, so
// that neighbors across the seam keep continuous positions. The canonical index, wrap(cell),
// identifies a cell: its hash, its draws, its cache key and every attribute computed at its
// position use the canonical index and the canonical position, so they are the same bits for
// every image; only distances to the query use the image, canonical position + shift(cell).
// Near the origin the canonical index is the unwrapped index itself.
struct PeriodicCells {
    std::int64_t count = 1;
    double size = 1.0;
    double period = 1.0;

    // Unwrapped index of the cell holding v: floor(v / size).
    [[nodiscard]] std::int64_t cellOf(double v) const;
    [[nodiscard]] std::int64_t wrap(std::int64_t cell) const { return wrapIndex(cell, count); }
    // Offset from the canonical image of the cell to this one: a whole number of periods.
    [[nodiscard]] double shift(std::int64_t cell) const;
};

// Throws std::invalid_argument unless period and nominal are finite and above 0.
[[nodiscard]] PeriodicCells periodicCells(double period, double nominal);

} // namespace terrain
