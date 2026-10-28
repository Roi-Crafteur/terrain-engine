#pragma once

// FROZEN REFERENCE, do not edit: stitchSeams as of commit 5e96585, before the seams between LODs
// followed the coarse outline (lod_seams.cpp), copied verbatim from terrain_core/src/streaming/
// lod.cpp. lod_seams_frozen_test compares the live stitching with it.

#include "terrain/meshing/density_grid.hpp"
#include "terrain/streaming/lod.hpp"

namespace frozen {

namespace stitch_detail {

using terrain::DensityGrid;
using terrain::kGridSamples;

struct GridPoint {
    int x = 0;
    int y = 0;
    int z = 0;
};

// A vertical face of the grid: a is the sample index along y, b along the horizontal axis.
struct Face {
    bool alongZ = false; // b runs along z (the face is at a fixed x), else along x
    int fixed = 0;       // index of the face on the other horizontal axis

    [[nodiscard]] GridPoint at(int a, int b) const {
        return alongZ ? GridPoint{fixed, a, b} : GridPoint{b, a, fixed};
    }
};

inline float read(const DensityGrid& grid, GridPoint p) {
    return grid.at(p.x, p.y, p.z);
}

inline void stitchFace(DensityGrid& grid, Face face) {
    for (int b = 0; b < kGridSamples; ++b) {
        for (int a = 0; a < kGridSamples; ++a) {
            const bool oddA = a % 2 != 0;
            const bool oddB = b % 2 != 0;
            float value = 0.0f;
            if (oddA && oddB) {
                value = (read(grid, face.at(a - 1, b - 1)) + read(grid, face.at(a + 1, b - 1)) +
                         read(grid, face.at(a - 1, b + 1)) + read(grid, face.at(a + 1, b + 1))) /
                        4.0f;
            } else if (oddA) {
                value = (read(grid, face.at(a - 1, b)) + read(grid, face.at(a + 1, b))) / 2.0f;
            } else if (oddB) {
                value = (read(grid, face.at(a, b - 1)) + read(grid, face.at(a, b + 1))) / 2.0f;
            } else {
                continue;
            }
            const GridPoint p = face.at(a, b);
            grid.set(p.x, p.y, p.z, value);
        }
    }
}

inline void stitchColumn(DensityGrid& grid, int x, int z) {
    for (int y = 1; y < kGridSamples; y += 2) {
        grid.set(x, y, z, (grid.at(x, y - 1, z) + grid.at(x, y + 1, z)) / 2.0f);
    }
}
} // namespace stitch_detail

inline void stitchSeams(terrain::DensityGrid& grid, unsigned seams) {
    using namespace stitch_detail;
    using namespace terrain;
    constexpr int kLast = kGridSamples - 1;
    if ((seams & kSeamNegX) != 0) {
        stitchFace(grid, {true, 0});
    }
    if ((seams & kSeamPosX) != 0) {
        stitchFace(grid, {true, kLast});
    }
    if ((seams & kSeamNegZ) != 0) {
        stitchFace(grid, {false, 0});
    }
    if ((seams & kSeamPosZ) != 0) {
        stitchFace(grid, {false, kLast});
    }
    // A corner column on a stitched face is already stitched the same way: no harm in redoing it.
    if ((seams & kSeamNegXNegZ) != 0) {
        stitchColumn(grid, 0, 0);
    }
    if ((seams & kSeamPosXNegZ) != 0) {
        stitchColumn(grid, kLast, 0);
    }
    if ((seams & kSeamNegXPosZ) != 0) {
        stitchColumn(grid, 0, kLast);
    }
    if ((seams & kSeamPosXPosZ) != 0) {
        stitchColumn(grid, kLast, kLast);
    }
}

} // namespace frozen
