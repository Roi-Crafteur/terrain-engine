#pragma once

#include "terrain/meshing/mesh.hpp"

namespace viewer {

// Chunk (0, 0) meshed from simple density functions, positions in world meters.

// Sphere of radius 6 m centered in the chunk.
[[nodiscard]] terrain::Mesh buildSphereChunk();

// Rolling ground between about 4.5 m and 11.5 m high.
[[nodiscard]] terrain::Mesh buildHeightmapChunk();

} // namespace viewer
