#pragma once

#include "altitude_color.hpp"
#include "horizon_layout.hpp"
#include "tile_mesh_conversion.hpp"
#include "unreal_frame.hpp"

#include "terrain/streaming/horizon_grids.hpp"
#include "terrain/streaming/horizon_sampler.hpp"

#include <cstdint>
#include <vector>

namespace terrain::unreal {

// How the far horizon shows the samples of its levels.
struct HorizonLook {
    // Every vertex lies this far below its ground, meters: on the band where the tiles of LOD 3
    // cover level 0 (TileHole), the tiles stay in front of it, without z-fighting.
    double dropMeters = 1.0;
    // The ocean, every sample of the sea (isSeaCell), flat at the sea level in kSeaColor (a lake
    // keeps its ground, and so does a dry depression below the sea level); and a sea square at
    // the sea level under the tiles.
    bool sea = true;
};

// A sample of the sea: flagged so by the core (kHorizonSea: where the geography puts the sea)
// and below the sea level.
[[nodiscard]] inline bool isSeaCell(const HorizonCell& cell) {
    return (cell.flags & kHorizonSea) != 0 && cell.height < kSeaLevel;
}

inline constexpr VertexColor kSeaColor{28, 66, 102, 255};

// A vertex of the horizon for a sample: its Unreal altitude (centimeters, relative to the
// altitude 0 of the core), its normal and its color by altitude (Bridge/altitude_color.hpp).
struct HorizonVertex {
    float z = 0.0f;
    UnrealVector3f normal;
    VertexColor color;
};

[[nodiscard]] HorizonVertex horizonVertex(const HorizonCell& cell, const HorizonLook& look);

// The triangles of the body of a level, computed once: the samples of the window outside the
// hole (the samples on its border included), the triangles of the cells outside it, a skirt
// under the border of the window, optionally the sea square, and a last vertex no triangle uses
// that brings the bounds of the mesh down to its lowest point once curved (fillBody).
//
// Vertices: the grid vertices first (samples, in rows of x), then one skirt vertex below each
// border vertex, the 4 corners of the sea square, and the bounds vertex. Triangles are front
// faced upward (the core order, unreal_frame.hpp), the skirt inward.
struct BodyTopology {
    int cells = 0;                       // K: the window holds K + 1 samples per side
    CellRect hole;                       // cells left out
    std::vector<std::int32_t> samples;   // per grid vertex: i + k * (K + 1)
    std::vector<std::int32_t> border;    // grid vertices of the window border, 4 K, from the low
                                         // corner along x, then z, then back
    std::int32_t skirtFirst = 0;         // skirt vertex of border[j]: skirtFirst + j
    std::int32_t seaFirst = -1;          // the sea square: corners (low, low), (high, low),
                                         // (low, high), (high, high); -1 without one
    std::int32_t boundsVertex = 0;
    std::vector<std::int32_t> triangles; // three per triangle

    [[nodiscard]] std::size_t vertexCount() const {
        return static_cast<std::size_t>(boundsVertex) + 1;
    }
};

// Throws std::invalid_argument unless cells is even, at least 2, and the hole lies inside the
// window without touching its border (an empty hole is allowed).
[[nodiscard]] BodyTopology bodyTopology(int cells, const CellRect& hole, bool seaSquare);

// What the body of one level needs besides its topology.
struct BodyShape {
    // A coarser level surrounds this one: each odd sample of the window border takes the mean of
    // its two neighbors along the border, so the border lies on the edges of the coarser level
    // (whose samples are the even ones, bit for bit) and leaves no crack.
    bool stitch = false;
    double skirtMeters = 2.0;
    double seaHalfMeters = 0.0; // half side of the sea square, around the window center
    // Lowest drop of the curvature over the window (curvatureDropMeters at levelReachMeters).
    double curveMeters = 0.0;
};

// Depth of the skirt of a level: max(2 m, pitch / 16), well beyond the cracks left by the
// rounding of the stitched borders and by the curvature between two vertices.
[[nodiscard]] double skirtMeters(int pitch);

// Fills the positions (centimeters from the low corner of the window, at the altitude 0), the
// normals and the colors of the body of a placed level; the triangles are the topology's.
void fillBody(const HorizonLevel& level, const BodyTopology& topology, const BodyShape& shape,
              const HorizonLook& look, UnrealMesh& mesh);

// The trim of a placed level: the cells of square outside finer, with their samples (no skirt:
// its edges meet the body of this level and the stitched border of the finer one). Throws
// std::invalid_argument unless finer lies inside square, inside the window.
[[nodiscard]] UnrealMesh levelTrim(const HorizonLevel& level, const CellRect& square,
                                   const CellRect& finer, const HorizonLook& look);

} // namespace terrain::unreal
