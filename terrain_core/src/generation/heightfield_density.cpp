#include "terrain/generation/heightfield_density.hpp"

#include <algorithm>
#include <cstddef>

namespace terrain {

ChunkColumns sampleChunkColumns(const Heightfield& field, double originX, double originZ) {
    ChunkColumns columns;
    columns.heights.reserve(static_cast<std::size_t>(kGridSamples * kGridSamples));
    for (int z = 0; z < kGridSamples; ++z) {
        for (int x = 0; x < kGridSamples; ++x) {
            columns.heights.push_back(
                field.heightAt(originX + x * kCellSizeMeters, originZ + z * kCellSizeMeters));
        }
    }
    const auto [low, high] = std::minmax_element(columns.heights.begin(), columns.heights.end());
    columns.minHeight = *low;
    columns.maxHeight = *high;
    return columns;
}

DensityGrid densityFromColumns(const ChunkColumns& columns, double originY) {
    DensityGrid grid(0.0f);
    for (int z = 0; z < kGridSamples; ++z) {
        for (int y = 0; y < kGridSamples; ++y) {
            const double sampleY = originY + y * kCellSizeMeters;
            for (int x = 0; x < kGridSamples; ++x) {
                // Same expression as HeightfieldDensity, so the values are identical.
                grid.set(x, y, z, static_cast<float>(sampleY - columns.at(x, z)));
            }
        }
    }
    return grid;
}

} // namespace terrain
