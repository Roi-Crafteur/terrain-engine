#include "heightfield_region.hpp"

#include "terrain/chunk_coord.hpp"
#include "terrain/generation/heightfield_density.hpp"
#include "terrain/meshing/marching_cubes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace viewer {

namespace {

// Coarse search grid: +-20 km around the origin, one sample per kilometer. The best sample
// is then refined on a finer grid that covers the coarse cells around it.
constexpr int kSearchSteps = 20;
constexpr double kSearchSpacing = 1000.0;
constexpr int kRefineSteps = 8;
constexpr double kRefineSpacing = 125.0;

// The camera starts above the highest ground, back from the south edge.
constexpr float kViewpointClearance = 30.0f;
constexpr double kViewpointSetback = 64.0;

// Highest sample of a (2 steps + 1)^2 grid around center.
GroundPoint highestSample(const terrain::Heightfield& field, GroundPoint center, int steps,
                          double spacing) {
    GroundPoint best = center;
    float highest = std::numeric_limits<float>::lowest();
    for (int j = -steps; j <= steps; ++j) {
        for (int i = -steps; i <= steps; ++i) {
            const GroundPoint p{center.x + i * spacing, center.z + j * spacing};
            const float height = field.heightAt(p.x, p.z);
            if (height > highest) {
                highest = height;
                best = p;
            }
        }
    }
    return best;
}

void appendMesh(terrain::Mesh& target, const terrain::Mesh& part, double originX, double originY,
                double originZ) {
    const auto first = static_cast<std::uint32_t>(target.vertices.size());
    for (const terrain::Vec3f& v : part.vertices) {
        target.vertices.push_back({static_cast<float>(originX + v.x),
                                   static_cast<float>(originY + v.y),
                                   static_cast<float>(originZ + v.z)});
    }
    target.normals.insert(target.normals.end(), part.normals.begin(), part.normals.end());
    for (const std::uint32_t index : part.indices) {
        target.indices.push_back(first + index);
    }
}

void meshColumn(const terrain::Heightfield& field, double originX, double originZ,
                HeightfieldRegion& region) {
    const terrain::ChunkColumns columns = terrain::sampleChunkColumns(field, originX, originZ);
    meshChunkColumn(columns, originX, originZ, region.mesh);
    region.minHeight = std::min(region.minHeight, columns.minHeight);
    region.maxHeight = std::max(region.maxHeight, columns.maxHeight);
}

} // namespace

GroundPoint findSummit(const terrain::Heightfield& field) {
    const GroundPoint coarse = highestSample(field, {0.0, 0.0}, kSearchSteps, kSearchSpacing);
    const GroundPoint fine = highestSample(field, coarse, kRefineSteps, kRefineSpacing);
    const double chunk = terrain::kChunkSizeMeters;
    return {std::round(fine.x / chunk) * chunk, std::round(fine.z / chunk) * chunk};
}

void meshChunkColumn(const terrain::ChunkColumns& columns, double originX, double originZ,
                     terrain::Mesh& mesh) {
    const double chunk = terrain::kChunkSizeMeters;
    const auto lowest = static_cast<int>(std::floor(columns.minHeight / chunk));
    const auto highest = static_cast<int>(std::floor(columns.maxHeight / chunk));
    for (int level = lowest; level <= highest; ++level) {
        const double originY = level * chunk;
        const terrain::Mesh part =
            terrain::marchingCubes(terrain::densityFromColumns(columns, originY));
        appendMesh(mesh, part, originX, originY, originZ);
    }
}

HeightfieldRegion buildHeightfieldRegion(const terrain::Heightfield& field) {
    const GroundPoint center = findSummit(field);
    const double half = kRegionChunks / 2 * terrain::kChunkSizeMeters;
    HeightfieldRegion region;
    region.minHeight = std::numeric_limits<float>::max();
    region.maxHeight = std::numeric_limits<float>::lowest();
    for (int j = 0; j < kRegionChunks; ++j) {
        for (int i = 0; i < kRegionChunks; ++i) {
            meshColumn(field, center.x - half + i * terrain::kChunkSizeMeters,
                       center.z - half + j * terrain::kChunkSizeMeters, region);
        }
    }
    region.viewpoint = {static_cast<float>(center.x), region.maxHeight + kViewpointClearance,
                        static_cast<float>(center.z + half + kViewpointSetback)};
    return region;
}

} // namespace viewer
