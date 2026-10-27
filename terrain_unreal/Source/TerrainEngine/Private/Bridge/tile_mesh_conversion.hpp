#pragma once

#include "altitude_color.hpp"
#include "unreal_frame.hpp"

#include "terrain/meshing/mesh.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain::unreal {

// A tile mesh as Unreal takes it: positions in centimeters relative to the origin of the tile,
// Z up; unit normals toward the air; one color per vertex, by altitude; three indices per
// triangle (int32, as the index arrays of Unreal), front faces as Unreal expects them.
struct UnrealMesh {
    std::vector<UnrealVector3f> positions;
    std::vector<UnrealVector3f> normals;
    std::vector<VertexColor> colors;
    std::vector<std::int32_t> triangles;

    [[nodiscard]] std::size_t triangleCount() const { return triangles.size() / 3; }
    [[nodiscard]] bool empty() const { return triangles.empty(); }

    friend bool operator==(const UnrealMesh&, const UnrealMesh&) = default;
};

// The sea of a tile, from its surface blend (TileData::surfaceBlend): the core gives the ocean
// biome exactly where it puts the sea (classifyBiome: WaterKind::Ocean, as kHorizonSea), so a
// point lies under the sea where the weight of the ocean, bilinear between the texels around it,
// is at least one half.
struct TileSea {
    const std::vector<BlendTexel>* texels = nullptr; // kTileBlendTexels^2 texels, or none
    double pitchMeters = 0.0;                        // between two texels
};

// The weight of the ocean at (x, z), meters from the lowest corner of the tile, bilinear between
// its texels (clamped to the tile). Throws std::invalid_argument unless the sea has
// kTileBlendTexels^2 texels and a positive pitch.
[[nodiscard]] double oceanShare(const TileSea& sea, double x, double z);

// Converts a mesh whose positions are relative to an origin at altitude originY (meters); the
// color of each vertex follows its altitude, originY + y (groundColor): the sea floor only where
// the sea of the tile is (oceanShare at least 1/2). Without texels, the altitude alone decides.
//
// The indices keep their order. Unreal draws the front face of a triangle (a, b, c) on the side
// of (c - a) x (b - a) (as FStaticMeshOperations computes face normals): the reverse of the core,
// whose counter-clockwise triangles have (b - a) x (c - a) toward the air. The swap of Y and Z
// already reverses cross products (unreal_frame.hpp), so the core order is the Unreal one.
//
// Throws std::invalid_argument unless there is one normal per vertex, three indices per
// triangle, every index names a vertex, and the vertices fit int32 indices.
[[nodiscard]] UnrealMesh convertMesh(const Mesh& mesh, double originY, const TileSea& sea = {});

// Unreal position of the origin of a tile mesh shown at an image of its tile. The world wraps
// around: the data of a tile lie in the canonical range of the world (data.request.key is
// canonical), and a client shows it at its image nearest to the player (nearestTile), so that
// the tiles across an edge of the world stand side by side. The origin moves by the whole
// worlds between the two keys, exactly.
[[nodiscard]] UnrealPoint tileOrigin(const UnrealFrame& frame, const TileData& data,
                                     const TileKey& image);

} // namespace terrain::unreal
