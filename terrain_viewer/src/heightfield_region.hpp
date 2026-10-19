#pragma once

#include "math.hpp"
#include "terrain/generation/heightfield.hpp"
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

// The region is centered on a summit: the highest point of a coarse, deterministic search
// around the origin, refined locally. Only the chunks crossed by the ground are meshed.
[[nodiscard]] HeightfieldRegion buildHeightfieldRegion(const terrain::Heightfield& field);

} // namespace viewer
