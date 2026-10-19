#pragma once

#include "terrain/generation/heightfield.hpp"
#include "terrain/meshing/density_grid.hpp"

#include <cstddef>
#include <vector>

namespace terrain {

// Terrain density of a heightfield: density(x, y, z) = y - heightAt(x, z).
// Negative (solid) below the ground, positive (air) above it.
class HeightfieldDensity {
public:
    explicit HeightfieldDensity(const Heightfield& field) : field_(&field) {}

    [[nodiscard]] double operator()(double x, double y, double z) const {
        return y - field_->heightAt(x, z);
    }

private:
    const Heightfield* field_;
};

// Ground heights of the kGridSamples x kGridSamples sample columns of a chunk.
struct ChunkColumns {
    std::vector<float> heights; // x varies fastest
    float minHeight = 0.0f;
    float maxHeight = 0.0f;

    [[nodiscard]] float at(int x, int z) const {
        return heights[static_cast<std::size_t>(z * kGridSamples + x)];
    }
};

// Heights of the columns at originX + i * kCellSizeMeters, originZ + k * kCellSizeMeters.
[[nodiscard]] ChunkColumns sampleChunkColumns(const Heightfield& field, double originX,
                                              double originZ);

// Density grid of the chunk whose lowest samples are at originY, built from its columns.
// Same values as sampleDensity(HeightfieldDensity(field), originX, originY, originZ), but
// heightAt is evaluated once per column instead of once per sample.
[[nodiscard]] DensityGrid densityFromColumns(const ChunkColumns& columns, double originY);

} // namespace terrain
