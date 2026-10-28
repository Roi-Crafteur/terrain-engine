#include "terrain/meshing/marching_cubes.hpp"
#include "terrain/streaming/lod.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace terrain {

namespace {

constexpr int kLast = kGridSamples - 1;

// Distance, in cells of the grid, within which a vertex or the center of a coarse cell counts as
// lying on the coarse outline: 0.06 mm at LOD 0, 0.24 mm at LOD 2 (the finer side of a seam is
// at most LOD 2). Where the density is linear along y (the natural ground, y - height), the
// stitched side already meets the coarse outline within float rounding (about 1e-7 cell):
// nothing moves there, the tiles keep their bits.
constexpr float kOnOutline = 1.0f / 16384.0f;

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

// The faces of a mask, in the order of their TileSeam bits.
constexpr std::array<std::pair<TileSeam, Face>, 4> kFaces{{
    {kSeamNegX, {true, 0}},
    {kSeamPosX, {true, kLast}},
    {kSeamNegZ, {false, 0}},
    {kSeamPosZ, {false, kLast}},
}};

float read(const DensityGrid& grid, GridPoint p) {
    return grid.at(p.x, p.y, p.z);
}

void stitchFace(DensityGrid& grid, Face face) {
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

void stitchColumn(DensityGrid& grid, int x, int z) {
    for (int y = 1; y < kGridSamples; y += 2) {
        grid.set(x, y, z, (grid.at(x, y - 1, z) + grid.at(x, y + 1, z)) / 2.0f);
    }
}

// A point of a face, in cells of the grid: a along y, b along the face.
struct FacePoint {
    float a = 0.0f;
    float b = 0.0f;
};

struct Chord {
    FacePoint from;
    FacePoint to;
};

// Twice the signed area of the triangle (chord, p): its sign tells the side of p.
float side(const Chord& c, FacePoint p) {
    return (c.to.a - c.from.a) * (p.b - c.from.b) - (c.to.b - c.from.b) * (p.a - c.from.a);
}

// True if p lies farther than kOnOutline from the line of the chord (never for a chord of zero
// length).
bool offChord(const Chord& c, FacePoint p) {
    const float da = c.to.a - c.from.a;
    const float db = c.to.b - c.from.b;
    const float s = side(c, p);
    return s * s > kOnOutline * kOnOutline * (da * da + db * db);
}

// The outline Marching Cubes draws on a coarse cell of a face, the square of 2 x 2 cells of the
// grid whose lowest corner is (a0, b0) (even samples, those the coarser tile samples), from its
// 4 corners alone. On each side crossed lies the vertex of the coarse mesh, interpolated from the
// lower sample as marchingCubes does; 2 sides crossed give one chord between their vertices, 4
// give a chord around each solid corner: the tables separate the solid corners of an ambiguous
// face (lod_seams_grid_test checks it for every case of a cell).
struct CoarseCell {
    // Corners in turn around the square: side k joins corner k to corner k + 1.
    std::array<FacePoint, 4> corner{};
    std::array<bool, 4> solid{};
    std::size_t chordCount = 0;
    std::array<Chord, 2> chords{};

    CoarseCell(const DensityGrid& grid, Face face, int a0, int b0);
};

CoarseCell::CoarseCell(const DensityGrid& grid, Face face, int a0, int b0) {
    const std::array<std::array<int, 2>, 4> at = {{{a0, b0}, {a0, b0 + 2}, {a0 + 2, b0 + 2},
                                                   {a0 + 2, b0}}};
    std::array<float, 4> value{};
    for (std::size_t k = 0; k < at.size(); ++k) {
        corner[k] = {static_cast<float>(at[k][0]), static_cast<float>(at[k][1])};
        value[k] = read(grid, face.at(at[k][0], at[k][1]));
        solid[k] = value[k] < kIsoLevel;
    }
    std::array<FacePoint, 4> crossing{};
    std::array<std::size_t, 4> crossed{};
    std::size_t count = 0;
    for (std::size_t k = 0; k < at.size(); ++k) {
        const std::size_t next = (k + 1) % at.size();
        if (solid[k] == solid[next]) {
            continue;
        }
        // Sides 0 and 1 run up the indices, sides 2 and 3 down.
        const std::size_t low = k < 2 ? k : next;
        const std::size_t high = k < 2 ? next : k;
        const float t =
            std::clamp((kIsoLevel - value[low]) / (value[high] - value[low]), 0.0f, 1.0f);
        const FacePoint from = corner[low];
        const FacePoint to = corner[high];
        crossing[k] = {from.a + (to.a - from.a) * t, from.b + (to.b - from.b) * t};
        crossed[count++] = k;
    }
    if (count == 2) {
        chords[chordCount++] = {crossing[crossed[0]], crossing[crossed[1]]};
    } else if (count == 4) {
        for (std::size_t k = 0; k < at.size(); ++k) {
            if (solid[k]) {
                chords[chordCount++] = {crossing[(k + 3) % 4], crossing[k]};
            }
        }
    }
}

enum class Region { Unknown, Solid, Air };

// The side of the coarse outline the center of the cell lies on; unknown within kOnOutline of
// a chord, or in a cell the outline does not cross.
Region centerRegion(const CoarseCell& cell) {
    const FacePoint center{cell.corner[0].a + 1.0f, cell.corner[0].b + 1.0f};
    if (cell.chordCount == 0) {
        return Region::Unknown;
    }
    for (std::size_t n = 0; n < cell.chordCount; ++n) {
        if (!offChord(cell.chords[n], center)) {
            return Region::Unknown;
        }
    }
    if (cell.chordCount == 2) {
        return Region::Air; // between the two chords around the solid corners
    }
    // One chord: the center lies on the side of the solid corners or not. The solid corner
    // farthest from the chord tells its solid side.
    float reference = 0.0f;
    for (std::size_t k = 0; k < cell.corner.size(); ++k) {
        const float s = side(cell.chords[0], cell.corner[k]);
        if (cell.solid[k] && std::abs(s) > std::abs(reference)) {
            reference = s;
        }
    }
    if (reference == 0.0f) {
        return Region::Unknown;
    }
    return (side(cell.chords[0], center) > 0.0f) == (reference > 0.0f) ? Region::Solid
                                                                       : Region::Air;
}

// The center of each coarse cell of the face (its odd/odd sample) keeps its bilinear value but
// takes the side of the coarse outline it lies on: every edge of the grid between the middle of
// a side of the cell and its center is then crossed by Marching Cubes exactly when a chord
// crosses it, and the fine outline follows the coarse one cell by cell.
void settleCenters(DensityGrid& grid, Face face) {
    for (int b0 = 0; b0 < kLast; b0 += 2) {
        for (int a0 = 0; a0 < kLast; a0 += 2) {
            const Region region = centerRegion(CoarseCell(grid, face, a0, b0));
            const GridPoint p = face.at(a0 + 1, b0 + 1);
            const float value = read(grid, p);
            if (region == Region::Unknown || (value < kIsoLevel) == (region == Region::Solid)) {
                continue;
            }
            float settled = std::abs(value);
            if (region == Region::Solid) {
                settled = settled > 0.0f ? -settled : -std::numeric_limits<float>::min();
            }
            grid.set(p.x, p.y, p.z, settled);
        }
    }
}

// Where the chord crosses the line `line` (a fixed coordinate along a or b), as a coordinate
// along that line; false if it does not cross it.
bool crossLine(const Chord& c, bool lineAlongB, float line, float& at) {
    const float l0 = lineAlongB ? c.from.a : c.from.b;
    const float l1 = lineAlongB ? c.to.a : c.to.b;
    const float r0 = lineAlongB ? c.from.b : c.from.a;
    const float r1 = lineAlongB ? c.to.b : c.to.a;
    if (l0 == l1 || (l0 - line) * (l1 - line) > 0.0f) {
        return false;
    }
    at = r0 + (line - l0) * (r1 - r0) / (l1 - l0);
    return true;
}

// Moves a vertex of the face lying inside a coarse cell, on an edge of the grid between the
// middle of a side of the cell and its center, along that edge onto the chord crossing it. The
// vertices on the sides of the coarse cells are those of the coarse mesh already.
void snapVertex(const DensityGrid& grid, Face face, Vec3f& v) {
    float& along = face.alongZ ? v.z : v.x;
    const bool wholeA = std::floor(v.y) == v.y;
    const bool wholeB = std::floor(along) == along;
    if (wholeA == wholeB) {
        return; // on a sample
    }
    // The edge runs along b at a fixed odd a (wholeA), or along a at a fixed odd b.
    const float line = wholeA ? v.y : along;
    float& run = wholeA ? along : v.y;
    const auto fixedIndex = static_cast<int>(line);
    if (fixedIndex % 2 == 0) {
        return;
    }
    const auto low = static_cast<int>(std::floor(run));
    const int runCell = low - low % 2;
    const CoarseCell cell = wholeA ? CoarseCell(grid, face, fixedIndex - 1, runCell)
                                   : CoarseCell(grid, face, runCell, fixedIndex - 1);
    float best = std::numeric_limits<float>::infinity();
    float target = run;
    for (std::size_t n = 0; n < cell.chordCount; ++n) {
        float at = 0.0f;
        if (!crossLine(cell.chords[n], wholeA, line, at)) {
            continue;
        }
        const float outside = std::max({static_cast<float>(low) - at, 0.0f,
                                        at - static_cast<float>(low + 1)});
        if (outside < best) {
            best = outside;
            target = std::clamp(at, static_cast<float>(low), static_cast<float>(low + 1));
        }
    }
    if (std::abs(target - run) > kOnOutline) {
        run = target;
    }
}

} // namespace

void stitchSeams(DensityGrid& grid, unsigned seams) {
    for (const auto& [seam, face] : kFaces) {
        if ((seams & seam) != 0) {
            stitchFace(grid, face);
            settleCenters(grid, face);
        }
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

Mesh stitchedMesh(const DensityGrid& grid, unsigned seams) {
    Mesh mesh = marchingCubes(grid);
    for (const auto& [seam, face] : kFaces) {
        if ((seams & seam) == 0) {
            continue;
        }
        const auto plane = static_cast<float>(face.fixed);
        for (Vec3f& v : mesh.vertices) {
            if ((face.alongZ ? v.x : v.z) == plane) {
                snapVertex(grid, face, v);
            }
        }
    }
    return mesh;
}

} // namespace terrain
