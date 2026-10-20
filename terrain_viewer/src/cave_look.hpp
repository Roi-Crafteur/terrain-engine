#pragma once

#include "math.hpp"
#include "terrain/generation/cave_materials.hpp"
#include "terrain/generation/speleothems.hpp"
#include "terrain/meshing/mesh.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include <vector>

namespace viewer {

// How the caves look, computed on the CPU (no OpenGL): colors of the materials, of the lakes and
// the geometry of the speleothems. Everything only decorates the tiles. The cave districts and
// the cave flora have their own files (district_look.hpp, flora_look.hpp).

// Opacity of the lakes (alpha blending).
inline constexpr float kLakeOpacity = 0.75f;

// Share of the color of the lava that the terrain shader shows unlit (uEmissive): the lava is
// drawn at full glow, so it is as bright in the dark as in the light of the headlight.
inline constexpr float kLavaEmissive = 1.0f;

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

// Color of each vertex of data.lava, an unlit glow (the lava is drawn with kLavaEmissive): yellow
// orange where the surface faces up (the normal y is 1), blending to a deeper orange where it
// faces sideways or down. Warm (red >= green >= blue), bright, within [0, 1] and apart from the
// colors of the materials and of the lakes. One color per vertex of data.lava.
[[nodiscard]] std::vector<Vec3> lavaColors(const terrain::TileData& data);

// The color that the terrain shader writes (kFragmentShader of cave_world_view.cpp mixes the same
// way): lit * (1 - emissive) + albedo * emissive. Emissive 0 gives the lit color, 1 the albedo
// whatever the light, in between a linear mix. The shader does not clamp emissive; neither does
// this mirror.
[[nodiscard]] Vec3 glowColor(Vec3 lit, Vec3 albedo, float emissive);

// An indexed mesh with a color per vertex (and a normal, in mesh).
struct PaintedMesh {
    terrain::Mesh mesh;
    std::vector<Vec3> colors;
};

// Speleothems as one indexed mesh in world meters, with a color per vertex: small cones whose
// tips point down (stalactites) or up (stalagmites), in the color of limestone or of ice.
using SpeleothemMesh = PaintedMesh;

[[nodiscard]] SpeleothemMesh speleothemMesh(const std::vector<terrain::Speleothem>& speleothems);

} // namespace viewer
