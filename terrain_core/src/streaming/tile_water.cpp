#include "tile_water.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace terrain::tile {

namespace {

// The water at (u, v) cells from the lowest corner of the lattice.
float waterAt(const TileColumns& columns, double u, double v) {
    u = std::clamp(u, 0.0, double{kChunkCells});
    v = std::clamp(v, 0.0, double{kChunkCells});
    const int i = std::min(static_cast<int>(u), kChunkCells - 1);
    const int k = std::min(static_cast<int>(v), kChunkCells - 1);
    const double fu = u - i;
    const double fv = v - k;
    const std::array<double, 4> weights{(1.0 - fu) * (1.0 - fv), fu * (1.0 - fv),
                                        (1.0 - fu) * fv, fu * fv};
    const std::array<float, 4> levels{columns.waterAt(i, k), columns.waterAt(i + 1, k),
                                      columns.waterAt(i, k + 1), columns.waterAt(i + 1, k + 1)};
    double weight = 0.0;
    double sum = 0.0;
    for (std::size_t n = 0; n < weights.size(); ++n) {
        if (weights[n] > 0.0 && levels[n] > kNoWaterLevel) {
            weight += weights[n];
            sum += weights[n] * levels[n];
        }
    }
    return weight > 0.0 ? static_cast<float>(sum / weight) : kNoWaterLevel;
}

} // namespace

std::vector<float> vertexWaterLevels(const TileColumns& columns, const TileLattice& lattice,
                                     const Mesh& mesh, std::span<const CaveMaterial> materials) {
    std::vector<float> levels;
    levels.reserve(mesh.vertices.size());
    const auto spacing = static_cast<double>(lattice.spacing);
    for (std::size_t n = 0; n < mesh.vertices.size(); ++n) {
        const Vec3f& vertex = mesh.vertices[n];
        const bool surface = materials.empty() || materials[n] == CaveMaterial::Surface;
        levels.push_back(surface ? waterAt(columns, vertex.x / spacing, vertex.z / spacing)
                                 : kNoWaterLevel);
    }
    return levels;
}

} // namespace terrain::tile
