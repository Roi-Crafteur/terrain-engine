#include "terrain/generation/height_grid.hpp"

#include <stdexcept>
#include <string>

namespace terrain {

HeightGrid sampleHeightGrid(const Heightfield& field, const HeightmapRegion& region) {
    HeightGrid grid{region.size, region.metersPerPixel, {}};
    grid.heights.reserve(static_cast<std::size_t>(region.size) *
                         static_cast<std::size_t>(region.size));
    const double half = region.size / 2.0;
    for (int row = 0; row < region.size; ++row) {
        const double z = region.centerZ + (row + 0.5 - half) * region.metersPerPixel;
        for (int column = 0; column < region.size; ++column) {
            const double x = region.centerX + (column + 0.5 - half) * region.metersPerPixel;
            grid.heights.push_back(field.heightAt(x, z));
        }
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
