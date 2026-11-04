// The light of the sides of the tiles of the ground (tile_sides.hpp).
#include "tile_sides.hpp"

#include "terrain/streaming/lod.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace terrain::tile {

namespace {

constexpr int kLast = kGridSamples - 1;

// The ground of the column (i, k), from the ring beyond the sides.
float groundAt(const TileColumns& c, int i, int k) {
    if (i >= 0 && i <= kLast && k >= 0 && k <= kLast) {
        return c.at(i, k);
    }
    const int side = i < 0 ? 0 : (i > kLast ? 1 : (k < 0 ? 2 : 3));
    return c.ringHeights[static_cast<std::size_t>(side * kGridSamples + (side < 2 ? k : i))];
}

// The gradient of the density y - ground at the column (i, k), per cell (Marching Cubes: the
// difference over two cells over 2), its y part the meters of a cell.
Vec3f gradientAt(const TileColumns& c, int i, int k, float spacing) {
    return {-(groundAt(c, i + 1, k) - groundAt(c, i - 1, k)) / 2.0f, spacing,
            -(groundAt(c, i, k + 1) - groundAt(c, i, k - 1)) / 2.0f};
}

bool onSide(float u, bool low, bool high) {
    return (low && std::abs(u) < 1e-4f) || (high && std::abs(u - static_cast<float>(kLast)) < 1e-4f);
}

} // namespace

void lightSides(Mesh& level, const TileColumns& columns, unsigned seams, int spacing) {
    if (columns.ringHeights.size() != static_cast<std::size_t>(4 * kGridSamples)) {
        return;
    }
    const bool negX = (seams & kSeamNegX) == 0;
    const bool posX = (seams & kSeamPosX) == 0;
    const bool negZ = (seams & kSeamNegZ) == 0;
    const bool posZ = (seams & kSeamPosZ) == 0;
    const auto s = static_cast<float>(spacing);
    for (std::size_t n = 0; n < level.vertices.size(); ++n) {
        const Vec3f& v = level.vertices[n];
        if (!onSide(v.x, negX, posX) && !onSide(v.z, negZ, posZ)) {
            continue;
        }
        // The vertex on a grid edge: along x or z between two columns (lerped), or along y.
        const int i0 = std::clamp(static_cast<int>(std::floor(v.x + 1e-4f)), 0, kLast);
        const int k0 = std::clamp(static_cast<int>(std::floor(v.z + 1e-4f)), 0, kLast);
        const float tx = std::clamp(v.x - static_cast<float>(i0), 0.0f, 1.0f);
        const float tz = std::clamp(v.z - static_cast<float>(k0), 0.0f, 1.0f);
        Vec3f g = gradientAt(columns, i0, k0, s);
        if (tx > 1e-4f) {
            const Vec3f h = gradientAt(columns, i0 + 1, k0, s);
            g = {g.x + tx * (h.x - g.x), g.y, g.z + tx * (h.z - g.z)};
        } else if (tz > 1e-4f) {
            const Vec3f h = gradientAt(columns, i0, k0 + 1, s);
            g = {g.x + tz * (h.x - g.x), g.y, g.z + tz * (h.z - g.z)};
        }
        const float length = std::sqrt(g.x * g.x + g.y * g.y + g.z * g.z);
        if (length > 1e-12f) {
            level.normals[n] = {g.x / length, g.y / length, g.z / length};
        }
    }
}

} // namespace terrain::tile
