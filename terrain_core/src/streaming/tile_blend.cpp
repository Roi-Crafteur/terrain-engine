#include "tile_blend.hpp"

#include "terrain/generation/terrain_surface.hpp"

#include <array>
#include <cstddef>
#include <stdexcept>

namespace terrain::tile {

namespace {

constexpr int kLastColumn = kGridSamples - 1;

// Sides of the tile, in the order of the heights of outsideHeights.
enum Side : int { kWest, kEast, kNorth, kSouth, kSides };

// Heights of the neighbors of the texels on the sides of the tile, one cell beyond them: texel t
// along a side (column or row t * stride) at index t * kSides + side.
std::vector<float> outsideHeights(const Heightfield& field, const TileLattice& lattice, int texels,
                                  int stride) {
    const double x0 = lattice.x0;
    const double z0 = lattice.z0;
    const double cell = lattice.spacing;
    const double beyond = (kLastColumn + 1) * cell;
    std::vector<double> xs;
    std::vector<double> zs;
    for (int t = 0; t < texels; ++t) {
        const double along = static_cast<double>(t) * stride * cell;
        const std::array<std::array<double, 2>, kSides> points = {{{x0 - cell, z0 + along},
                                                                   {x0 + beyond, z0 + along},
                                                                   {x0 + along, z0 - cell},
                                                                   {x0 + along, z0 + beyond}}};
        for (const auto& [x, z] : points) {
            xs.push_back(x);
            zs.push_back(z);
        }
    }
    std::vector<float> heights(xs.size());
    field.heightsAt(xs, zs, heights);
    return heights;
}

} // namespace

std::vector<BlendTexel> blendTexels(const Heightfield& field, const Ecology& ecology,
                                    const TileLattice& lattice, const TileColumns& columns,
                                    int stride) {
    if (stride <= 0 || kLastColumn % stride != 0) {
        throw std::invalid_argument("blendTexels: the stride must divide the cells of a tile");
    }
    const int texels = kLastColumn / stride + 1;
    const std::vector<float> outside = outsideHeights(field, lattice, texels, stride);
    const auto at = [&outside, stride](int along, Side side) {
        return outside[static_cast<std::size_t>((along / stride) * kSides + side)];
    };
    const auto height = [&](int i, int k) {
        if (i < 0) {
            return at(k, kWest);
        }
        if (i > kLastColumn) {
            return at(k, kEast);
        }
        if (k < 0) {
            return at(i, kNorth);
        }
        return k > kLastColumn ? at(i, kSouth) : columns.at(i, k);
    };
    std::vector<BlendTexel> result;
    result.reserve(static_cast<std::size_t>(texels) * static_cast<std::size_t>(texels));
    for (int k = 0; k <= kLastColumn; k += stride) {
        const double z = lattice.z0 + static_cast<double>(k) * lattice.spacing;
        for (int i = 0; i <= kLastColumn; i += stride) {
            const double x = lattice.x0 + static_cast<double>(i) * lattice.spacing;
            const SurfaceSite site = heightfieldSite(
                columns.at(i, k), columns.continentalnessAt(i, k),
                {height(i + 1, k), height(i - 1, k), height(i, k + 1), height(i, k - 1)},
                lattice.spacing);
            const ClimateSample climate = ecology.climate().sample(x, z, site);
            result.push_back(packBlend(ecology.blendAt(x, z, site, climate)));
        }
    }
    return result;
}

} // namespace terrain::tile
