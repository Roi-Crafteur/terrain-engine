#include "demo_chunks.hpp"

#include "terrain/meshing/marching_cubes.hpp"

#include <cmath>

namespace viewer {

namespace {

constexpr double kChunkCenter = terrain::kChunkSizeMeters / 2.0;
constexpr double kSphereRadius = 6.0;

double sphereDensity(double x, double y, double z) {
    const double dx = x - kChunkCenter;
    const double dy = y - kChunkCenter;
    const double dz = z - kChunkCenter;
    return std::sqrt(dx * dx + dy * dy + dz * dz) - kSphereRadius;
}

double heightmapDensity(double x, double y, double z) {
    const double height =
        8.0 + 2.5 * std::sin(0.45 * x) * std::cos(0.35 * z) + std::sin(0.9 * (x + z));
    return y - height;
}

} // namespace

terrain::Mesh buildSphereChunk() {
    return terrain::marchingCubes(terrain::sampleDensity(sphereDensity, 0.0, 0.0, 0.0));
}

terrain::Mesh buildHeightmapChunk() {
    return terrain::marchingCubes(terrain::sampleDensity(heightmapDensity, 0.0, 0.0, 0.0));
}

} // namespace viewer
