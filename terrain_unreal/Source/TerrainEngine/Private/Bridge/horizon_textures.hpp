#pragma once

#include "horizon_layout.hpp"
#include "horizon_mesh.hpp"
#include "terrain_surface.hpp"

#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/horizon_epoch.hpp"
#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/horizon_sampler.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace terrain::unreal {

// The GPU horizon (phase 13, step 2). Each level is a texture of (K + 1) x (K + 1) texels, one
// per sample at the slot of its toroidal buffer (sample s lies in texel slotOf(s)), drawn by a
// fixed grid whose material reads it: moving a level only writes the texels of the samples that
// enter its window.
//
// A texel is 4 floats: the height shown (meters, the sea flat at the sea level where isSeaCell,
// never a dry depression below it), the slopes of the ground along x and z (0 on the ocean,
// whose normal points up), and the surface of the ground, its material and its biome packed by
// packSurface (terrain_surface.hpp), at least 0, with the water code (kSurfaceWaterCode) under a
// lake or a river: the ground of a dry depression below the sea level never has it.
// On the ocean the last float is instead the ground under the sea, meters from the sea level,
// below 0: the material colors the sea by its depth (texelGround). The materials of the horizon
// and of the tiles take the layers of the ground from the surface (the temporary terrain
// material of phase 14, Scripts/temporary_terrain_look.py).
inline constexpr int kTexelFloats = 4;

// The material of the horizon draws the sea at the altitude 0.
static_assert(kSeaLevel == 0.0f, "the sea of the horizon lies at the altitude 0");

struct HorizonTexel {
    float height = 0.0f;
    float gradX = 0.0f;
    float gradZ = 0.0f;
    float color = 0.0f;

    friend bool operator==(const HorizonTexel&, const HorizonTexel&) = default;
};

[[nodiscard]] HorizonTexel horizonTexel(const HorizonCell& cell, bool sea);

// A texel of the ocean (horizonTexel with the sea): its last float is negative.
[[nodiscard]] bool isSeaTexel(const HorizonTexel& texel);
// The ground the material colors the sea from, meters from the sea level: under the sea for a
// texel of the ocean (below 0), the height shown, at least 0, elsewhere. The material
// interpolates it between the samples: the coast lies where it crosses 0.
[[nodiscard]] float texelGround(const HorizonTexel& texel);

// The texels of a computed piece, kTexelFloats floats per cell, row by row as its cells.
[[nodiscard]] std::vector<float> pieceTexels(const HorizonPiece& piece, bool sea);

// The water of the GPU horizon (task 7b): a second texture per level, one float per texel (R32F,
// PF_R32_FLOAT in Unreal), at the slots of the texels: the level of the water over the sample
// (HorizonCell::waterLevel: the sea at 0, a lake, a river), bit for bit the level of the core,
// so that the water of the horizon meets that of the tiles without a step (agent 27); where no
// water stands, kNoWaterLevel. A material takes a texel below kWetWaterTexel for dry land.
inline constexpr float kWetWaterTexel = -1.0e8f;
static_assert(kNoWaterLevel < kWetWaterTexel, "dry land reads below the wet threshold");

// The water texel of a level: the level itself, kNoWaterLevel where none (or not a number).
[[nodiscard]] float waterTexel(float level);

// What the GPU horizon uploads for a computed piece: the texels of pieceTexels, then the water
// texel of each of its cells in the same order, one float each.
[[nodiscard]] std::vector<float> pieceUploads(const HorizonPiece& piece, bool sea);

// Texels to write into the texture of a level: slots [x, x + width) x [z, z + height), their
// floats row by row.
struct TexelRegion {
    int level = 0;
    int x = 0;
    int z = 0;
    int width = 0;
    int height = 0;
    std::vector<float> data;
};

// A run of samples along an axis and the slots it lands in, without wrapping.
struct SlotSpan {
    std::int64_t sample = 0; // first sample
    int slot = 0;            // its slot
    int length = 0;
};

// The samples [first, last) of an axis (at most side) as at most 2 runs, split where their slots
// wrap around.
[[nodiscard]] std::vector<SlotSpan> slotSpans(std::int64_t first, std::int64_t last, int side);

// The regions of the texels of a rectangle of samples of a level, given as pieceTexels lays
// them out: at most 4, split where the slots wrap around. Throws std::invalid_argument for data
// of another size or a rectangle wider than the texture.
[[nodiscard]] std::vector<TexelRegion> texelRegions(int level, int side, const HorizonRect& rect,
                                                    std::span<const float> data);

// Water texels to write into the water texture of a level, as a TexelRegion.
struct WaterRegion {
    int level = 0;
    int x = 0;
    int z = 0;
    int width = 0;
    int height = 0;
    std::vector<float> data;
};

// The regions of the water texels of a rectangle of samples of a level, given as pieceUploads
// lays them out after the texels (one float per sample): split as texelRegions. Throws
// std::invalid_argument as texelRegions.
[[nodiscard]] std::vector<WaterRegion> waterRegions(int level, int side, const HorizonRect& rect,
                                                    std::span<const float> levels);

// The fixed grids of the GPU horizon, in cells: the vertex (i, k) of a level stands (i, k)
// pitches from the low corner of its window, its height read from the texel of its sample.
enum class GridVertexKind : std::uint8_t {
    Sample, // the sample (i, k)
    Skirt,  // below the sample (i, k) of the border, by the depth of the skirt
    Sea     // a corner of the sea square, at the sea level
};

struct GridVertex {
    float i = 0.0f;
    float k = 0.0f;
    GridVertexKind kind = GridVertexKind::Sample;

    friend bool operator==(const GridVertex&, const GridVertex&) = default;
};

struct GridMesh {
    std::vector<GridVertex> vertices;
    std::vector<std::int32_t> triangles; // three per triangle, front faced as the CPU meshes

    [[nodiscard]] std::size_t triangleCount() const { return triangles.size() / 3; }
};

// The body of a level: its topology (bodyTopology) without its bounds vertex (the bounds of the
// GPU meshes are set on them), its sea square seaHalfCells around the center of the window.
[[nodiscard]] GridMesh bodyGrid(const BodyTopology& topology, double seaHalfCells);

// The trims of levels 1 and up, the same in cells for all: the cells of the square of the body
// hole outside the finer window, which stands (dx, dz) cells off its center, each in [-1, 1].
// Variant (dx + 1) + 3 (dz + 1).
inline constexpr int kTrimVariants = 9;
[[nodiscard]] GridMesh trimGrid(int cells, int dx, int dz);

// The variant of the trim around a finer window (finerWindowCells). Throws
// std::invalid_argument for a window more than one cell off center.
[[nodiscard]] int trimVariant(const CellRect& finer, int cells);

} // namespace terrain::unreal
