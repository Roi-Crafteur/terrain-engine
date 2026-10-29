#include "rock_tile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace terrain::tile {

namespace {

constexpr int kLast = kGridSamples - 1;
// On a side stitched to a coarser tile, the density of the rock turns from the tile's detail to
// the coarser one's over kSeamBlend cells (the faces the coarser tile does not draw fade out, not
// cut short at the side).
constexpr double kSeamBlend = 4.0;

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// The derivative at index of samples f(index) along a side of kGridSamples of them: centered
// within and, with samples one step beyond the side (ring), at its ends too; second order
// differences there otherwise.
template <typename Sample>
float derivative(int index, bool ring, const Sample& f) {
    if (ring) {
        return 0.5f * (f(index + 1) - f(index - 1));
    }
    if (index == 0) {
        return 0.5f * (-3.0f * f(0) + 4.0f * f(1) - f(2));
    }
    if (index == kLast) {
        return 0.5f * (3.0f * f(kLast) - 4.0f * f(kLast - 1) + f(kLast - 2));
    }
    return 0.5f * (f(index + 1) - f(index - 1));
}

} // namespace

RockTile::RockTile(const TileColumns& columns, const TileRequest& request,
                   const TileLattice& lattice)
    : columns_(&columns), lattice_(lattice), seams_(request.seams), horizon_(request.horizon),
      fine_(rockDetailForLod(request.key.lod)), coarse_(fine_) {
    if (request.seams != 0 && request.key.lod < kMaxLod) {
        coarse_ = rockDetailForLod(request.key.lod + 1);
        resample_ = !(coarse_ == fine_);
    }
    bounds_ = {columns.minHeight, columns.maxHeight};
    for (const RockColumn& c : columns.rock) {
        const RockBounds b = rockBounds(c);
        bounds_.low = std::min(bounds_.low, b.low);
        bounds_.high = std::max(bounds_.high, b.high);
    }
}

const RockColumn& RockTile::rockAt(int i, int k) const {
    return columns_->rock[static_cast<std::size_t>(k * kGridSamples + i)];
}

namespace {

// The index of column (i, k) one step beyond a side in the ring (tile::sampleRing).
std::size_t ringIndex(int i, int k) {
    const int side = i < 0 ? 0 : (i > kLast ? 1 : (k < 0 ? 2 : 3));
    return static_cast<std::size_t>(side * kGridSamples + (side < 2 ? k : i));
}

bool inside(int i, int k) {
    return i >= 0 && i <= kLast && k >= 0 && k <= kLast;
}

} // namespace

const RockColumn& RockTile::columnAt(int i, int k) const {
    return inside(i, k) ? rockAt(i, k) : columns_->ring[ringIndex(i, k)];
}

float RockTile::groundAt(int i, int k) const {
    return inside(i, k) ? columns_->at(i, k) : columns_->ringHeights[ringIndex(i, k)];
}

float RockTile::sample(int i, int j, int k, int originY, bool coarse) const {
    return sampleWith(i, j, k, originY, coarse ? coarse_ : fine_);
}

float RockTile::sampleWith(int i, int j, int k, int originY, const RockDetail& detail) const {
    const double sampleY = originY + j * lattice_.spacing;
    const RockColumn& c = columnAt(i, k);
    if (c.faceCount == 0 || onHorizonSide(i, k)) {
        // Same expression as tile::levelDensity and TerrainTiles::densityAt.
        return static_cast<float>(sampleY - groundAt(i, k));
    }
    return rockDensity(c, sampleY, detail);
}

float RockTile::sampleBlended(int i, int j, int k, int originY, double share) const {
    if (share >= 1.0) {
        return sampleWith(i, j, k, originY, fine_);
    }
    const float coarse = sampleWith(i, j, k, originY, coarse_);
    if (share <= 0.0) {
        return coarse;
    }
    const float fine = sampleWith(i, j, k, originY, fine_);
    return static_cast<float>(coarse + share * (fine - coarse));
}

double RockTile::fineShare(int i, int k) const {
    if (!resample_) {
        return 1.0;
    }
    i = std::clamp(i, 0, kLast);
    k = std::clamp(k, 0, kLast);
    const auto stitched = [this](unsigned seam) { return (seams_ & seam) != 0; };
    // Cells to the nearest stitched side (a corner: to the corner column).
    int cells = kGridSamples;
    const int toX[2] = {i, kLast - i};
    const int toZ[2] = {k, kLast - k};
    const unsigned sideX[2] = {kSeamNegX, kSeamPosX};
    const unsigned sideZ[2] = {kSeamNegZ, kSeamPosZ};
    const unsigned corner[2][2] = {{kSeamNegXNegZ, kSeamNegXPosZ}, {kSeamPosXNegZ, kSeamPosXPosZ}};
    for (int a = 0; a < 2; ++a) {
        cells = stitched(sideX[a]) ? std::min(cells, toX[a]) : cells;
        cells = stitched(sideZ[a]) ? std::min(cells, toZ[a]) : cells;
        for (int b = 0; b < 2; ++b) {
            cells = stitched(corner[a][b]) ? std::min(cells, std::max(toX[a], toZ[b])) : cells;
        }
    }
    return smooth(0.0, kSeamBlend, static_cast<double>(cells));
}

DensityGrid RockTile::levelDensity(int originY) const {
    // On the stitched sides the coarser tile samples the same points with its own detail.
    DensityGrid grid(0.0f);
    for (int k = 0; k < kGridSamples; ++k) {
        for (int i = 0; i < kGridSamples; ++i) {
            const double share = fineShare(i, k);
            for (int j = 0; j < kGridSamples; ++j) {
                grid.set(i, j, k, sampleBlended(i, j, k, originY, share));
            }
        }
    }
    return grid;
}

bool RockTile::onHorizonSide(int i, int k) const {
    return (i <= 0 && (horizon_ & kSeamNegX) != 0) || (i >= kLast && (horizon_ & kSeamPosX) != 0) ||
           (k <= 0 && (horizon_ & kSeamNegZ) != 0) || (k >= kLast && (horizon_ & kSeamPosZ) != 0);
}

void RockTile::dropPlants(std::vector<Plant>& plants) const {
    const double spacing = lattice_.spacing;
    std::erase_if(plants, [&](const Plant& plant) {
        const auto i = static_cast<int>(std::lround((plant.x - lattice_.x0) / spacing));
        const auto k = static_cast<int>(std::lround((plant.z - lattice_.z0) / spacing));
        const RockColumn& c = rockAt(std::clamp(i, 0, kLast), std::clamp(k, 0, kLast));
        return std::any_of(c.faces.begin(), c.faces.begin() + c.faceCount,
                           [this](const RockFace& f) { return drawnWeight(f, fine_) > 0.0f; });
    });
}

Vec3f RockTile::gradientAt(const DensityGrid& grid, int i, int j, int k, int originY) const {
    const bool ring = hasRing();
    const double share = fineShare(i, k);
    const float gx = derivative(i, ring, [&](int n) {
        return n >= 0 && n <= kLast ? grid.at(n, j, k) : sampleBlended(n, j, k, originY, share);
    });
    const float gz = derivative(k, ring, [&](int n) {
        return n >= 0 && n <= kLast ? grid.at(i, j, n) : sampleBlended(i, j, n, originY, share);
    });
    // Along y, centered through the rock beyond the level on its borders.
    const float below = j > 0 ? grid.at(i, j - 1, k) : sampleBlended(i, -1, k, originY, share);
    const float above =
        j < kLast ? grid.at(i, j + 1, k) : sampleBlended(i, kGridSamples, k, originY, share);
    return {gx, 0.5f * (above - below), gz};
}

void RockTile::lightLevel(Mesh& level, const DensityGrid& grid, int originY) const {
    for (std::size_t n = 0; n < level.vertices.size(); ++n) {
        const Vec3f& v = level.vertices[n];
        // The grid edge the vertex lies on: two of its coordinates are whole.
        const std::array<float, 3> p{v.x, v.y, v.z};
        std::array<int, 3> low{};
        int axis = -1;
        float t = 0.0f;
        for (int a = 0; a < 3; ++a) {
            const auto u = static_cast<std::size_t>(a);
            const float whole = std::round(p[u]);
            low[u] = std::clamp(static_cast<int>(whole), 0, kLast);
            if (std::abs(p[u] - whole) > 1e-4f && axis < 0) {
                axis = a;
                low[u] = std::clamp(static_cast<int>(std::floor(p[u])), 0, kLast - 1);
                t = p[u] - static_cast<float>(low[u]);
            }
        }
        Vec3f g = gradientAt(grid, low[0], low[1], low[2], originY);
        if (axis >= 0) {
            std::array<int, 3> high = low;
            ++high[static_cast<std::size_t>(axis)];
            const Vec3f h = gradientAt(grid, high[0], high[1], high[2], originY);
            g = {g.x + t * (h.x - g.x), g.y + t * (h.y - g.y), g.z + t * (h.z - g.z)};
        }
        const float length = std::sqrt(g.x * g.x + g.y * g.y + g.z * g.z);
        if (length > 1e-12f) {
            level.normals[n] = {g.x / length, g.y / length, g.z / length};
        }
    }
}

std::vector<float> RockTile::vertexRock(const Mesh& mesh, double originY) const {
    std::vector<float> rock;
    rock.reserve(mesh.vertices.size());
    const auto spacing = static_cast<float>(lattice_.spacing);
    for (const Vec3f& v : mesh.vertices) {
        const auto i = static_cast<int>(std::lround(v.x / spacing));
        const auto k = static_cast<int>(std::lround(v.z / spacing));
        const RockColumn& c = rockAt(std::clamp(i, 0, kLast), std::clamp(k, 0, kLast));
        const double y = originY + static_cast<double>(v.y);
        const double share = fineShare(i, k);
        const float fine = rockShare(c, y, fine_);
        if (share >= 1.0) {
            rock.push_back(fine);
            continue;
        }
        const float coarse = rockShare(c, y, coarse_);
        rock.push_back(static_cast<float>(coarse + share * (fine - coarse)));
    }
    return rock;
}

} // namespace terrain::tile
