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

// Converts a mesh whose positions are relative to an origin at altitude originY (meters); the
// color of each vertex follows its altitude, originY + y.
//
// The indices keep their order. Unreal draws the front face of a triangle (a, b, c) on the side
// of (c - a) x (b - a) (as FStaticMeshOperations computes face normals): the reverse of the core,
// whose counter-clockwise triangles have (b - a) x (c - a) toward the air. The swap of Y and Z
// already reverses cross products (unreal_frame.hpp), so the core order is the Unreal one.
//
// Throws std::invalid_argument unless there is one normal per vertex, three indices per
// triangle, every index names a vertex, and the vertices fit int32 indices.
[[nodiscard]] UnrealMesh convertMesh(const Mesh& mesh, double originY);

// Unreal position of the origin of a tile mesh.
// TODO(phase 9): in the wrapped world, the image of the canonical origin of the tile nearest to
// the player (nearestImage), so that the tiles across an edge of the world stand side by side.
[[nodiscard]] UnrealPoint tileOrigin(const UnrealFrame& frame, const TileData& data);

} // namespace terrain::unreal
