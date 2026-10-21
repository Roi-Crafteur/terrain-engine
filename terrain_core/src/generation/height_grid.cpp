#include "terrain/generation/height_grid.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace terrain {

// Row by row through Heightfield::heightsAt: neighboring pixels share noise cells.
HeightGrid sampleHeightGrid(const Heightfield& field, const HeightmapRegion& region) {
    const auto size = static_cast<std::size_t>(region.size);
    HeightGrid grid{region.size, region.metersPerPixel, std::vector<float>(size * size)};
    const double half = region.size / 2.0;
    std::vector<double> xs(size);
    for (int column = 0; column < region.size; ++column) {
        xs[static_cast<std::size_t>(column)] =
            region.centerX + (column + 0.5 - half) * region.metersPerPixel;
    }
    std::vector<double> zs(size);
    for (int row = 0; row < region.size; ++row) {
        const double z = region.centerZ + (row + 0.5 - half) * region.metersPerPixel;
        std::fill(zs.begin(), zs.end(), z);
        field.heightsAt(xs, zs, std::span<float>(grid.heights).subspan(grid.index(0, row), size));
    }
    return grid;
}

HeightmapRegion validatedRegion(const HeightmapRegion& region, const WorldSize& world) {
    const double side = region.size * region.metersPerPixel;
    if (!(side <= world.x && side <= world.z)) {
        throw std::invalid_argument("HeightmapRegion: a window of " + std::to_string(side) +
                                    " m is wider than the world (" + std::to_string(world.x) +
                                    " x " + std::to_string(world.z) + " m)");
    }
    return region;
}

} // namespace terrain
