#pragma once

#include "math.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/heightfield_density.hpp"
#include "terrain/meshing/mesh.hpp"

namespace viewer {

// Square of kRegionChunks x kRegionChunks chunks around a mountain of the heightfield.
inline constexpr int kRegionChunks = 32;

// Terrain density(x, y, z) = y - heightAt(x, z), meshed chunk by chunk and merged into a
// single mesh in world meters.
struct HeightfieldRegion {
    terrain::Mesh mesh;
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
    Vec3 viewpoint; // south of the region, above it, looking north (-Z) toward the center
};

// The region is centered on a summit (see findSummit). Only the chunks crossed by the ground
// are meshed.
[[nodiscard]] HeightfieldRegion buildHeightfieldRegion(const terrain::Heightfield& field);

struct GroundPoint {
    double x;
    double z;
};

// Summit near the origin: the highest point of a coarse, deterministic search, refined
// locally and snapped to the chunk grid.
[[nodiscard]] GroundPoint findSummit(const terrain::Heightfield& field);

// Meshes with Marching Cubes the chunks of one column, whose lowest samples are at
// (originX, originZ), between its lowest and highest ground, and appends them to mesh.
void meshChunkColumn(const terrain::ChunkColumns& columns, double originX, double originZ,
                     terrain::Mesh& mesh);

} // namespace viewer
