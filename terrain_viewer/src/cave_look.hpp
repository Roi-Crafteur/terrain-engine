#pragma once

#include "math.hpp"
#include "terrain/generation/cave_materials.hpp"
#include "terrain/generation/speleothems.hpp"
#include "terrain/meshing/mesh.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include <vector>

namespace viewer {

// How the caves look, computed on the CPU (no OpenGL): colors of the materials, of the lakes and
// the geometry of the speleothems. Everything only decorates the tiles.

// Opacity of the lakes (alpha blending).
inline constexpr float kLakeOpacity = 0.75f;

// Color of a material of the rock or of a lake. Surface, which is tinted by altitude, gives the
// lowland color.
[[nodiscard]] Vec3 caveMaterialColor(terrain::CaveMaterial material);

// Tint of the outer surface at altitude y meters: green at kStreamedLowAltitude, gray from
// kStreamedHighAltitude, as in the shader of the streamed world.
[[nodiscard]] Vec3 altitudeColor(float y);

// Color of each vertex of data.mesh: its material, or the tint of its altitude for the outer
// surface (and for every vertex when the tile carries no materials).
[[nodiscard]] std::vector<Vec3> rockColors(const terrain::TileData& data);

// Color of each vertex of data.water: the color of ice or of water.
[[nodiscard]] std::vector<Vec3> lakeColors(const terrain::TileData& data);

// Speleothems as one indexed mesh in world meters, with a color per vertex: small cones whose
// tips point down (stalactites) or up (stalagmites), in the color of limestone or of ice.
struct SpeleothemMesh {
    terrain::Mesh mesh;
    std::vector<Vec3> colors;
};

[[nodiscard]] SpeleothemMesh speleothemMesh(const std::vector<terrain::Speleothem>& speleothems);

} // namespace viewer
