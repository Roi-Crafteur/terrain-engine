#pragma once

// Where the water of a fall runs down its face (unreal_fall_lips_test): between the edge of its
// lip and where its water meets the pool, from the data of the fall only.
#include "terrain/streaming/stream_manager.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace falls {

// The distance from (x, z) to the quadrilateral q (its corners around it; 0 inside).
inline double toQuad(const std::array<std::array<double, 2>, 4>& q, double x, double z) {
    bool in = false;
    double nearest = std::numeric_limits<double>::infinity();
    for (std::size_t a = 0, b = 3; a < 4; b = a++) {
        const double ax = q[a][0], az = q[a][1], bx = q[b][0], bz = q[b][1];
        if ((az > z) != (bz > z) && x < ax + (bx - ax) * (z - az) / (bz - az)) {
            in = !in;
        }
        const double ex = bx - ax, ez = bz - az, length = ex * ex + ez * ez;
        const double t =
            length > 0.0 ? std::clamp(((x - ax) * ex + (z - az) * ez) / length, 0.0, 1.0) : 0.0;
        nearest = std::min(nearest, std::hypot(x - ax - t * ex, z - az - t * ez));
    }
    return in ? 0.0 : nearest;
}

// Whether (x, z) at level lies on the face of a fall of t (here or near): more than tolerance
// meters over the water below it and under the water above it, between the edge of its lip and
// where its water meets the pool (margin meters beyond them at most): its rock, which its curtain
// covers where its water falls and leaves bare beside it.
inline bool onFace(const terrain::TileData& t, double x, double z, double level, double tolerance,
            double margin) {
    for (const terrain::TileFall& f : t.falls) {
        if (!(level > static_cast<double>(f.bottom) + tolerance) ||
            !(level < static_cast<double>(f.top) - tolerance)) {
            continue;
        }
        const double dx = f.footX - f.x, dz = f.footZ - f.z;
        const double run = std::max(std::hypot(dx, dz), 1e-9);
        std::array<std::array<double, 2>, terrain::kFallEdge> lip{}, foot{};
        for (std::size_t p = 0; p < terrain::kFallEdge; ++p) {
            const double w = 0.5 * static_cast<double>(f.width) *
                             (2.0 * static_cast<double>(p) / (terrain::kFallEdge - 1.0) - 1.0);
            const bool shaped = f.edge == terrain::kFallEdge;
            lip[p] = shaped ? std::array<double, 2>{f.edgeX[p], f.edgeZ[p]}
                            : std::array<double, 2>{f.x - dz / run * w, f.z + dx / run * w};
            foot[p] = shaped ? std::array<double, 2>{f.footEdgeX[p], f.footEdgeZ[p]}
                             : std::array<double, 2>{lip[p][0] + dx, lip[p][1] + dz};
        }
        for (std::size_t p = 0; p + 1 < terrain::kFallEdge; ++p) {
            if (toQuad({lip[p], lip[p + 1], foot[p + 1], foot[p]}, x, z) <= margin) {
                return true;
            }
        }
    }
    return false;
}

} // namespace falls
