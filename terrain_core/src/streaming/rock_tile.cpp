#include "rock_tile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace terrain::tile {

namespace {

constexpr int kLast = kGridSamples - 1;

// The derivative at index of samples f(index) along a side of kGridSamples of them: centered
// within, second order differences at both ends.
template <typename Sample>
float derivative(int index, const Sample& f) {
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

float RockTile::sample(int i, int j, int k, int originY, bool coarse) const {
    const double sampleY = originY + j * lattice_.spacing;
    const RockColumn& c = rockAt(i, k);
    if (c.faceCount == 0 || onHorizonSide(i, k)) {
        // Same expression as tile::levelDensity and TerrainTiles::densityAt.
        return static_cast<float>(sampleY - columns_->at(i, k));
    }
    return rockDensity(c, sampleY, coarse ? coarse_ : fine_);
}

DensityGrid RockTile::levelDensity(int originY) const {
    DensityGrid grid(0.0f);
    for (int k = 0; k < kGridSamples; ++k) {
        for (int j = 0; j < kGridSamples; ++j) {
            for (int i = 0; i < kGridSamples; ++i) {
                grid.set(i, j, k, sample(i, j, k, originY, false));
            }
        }
    }
    if (!resample_) {
        return grid;
    }
    // The coarser tile samples the even lattice points of the side (as CaveTile::resampleSeams).
    for (int k = 0; k < kGridSamples; ++k) {
        for (int i = 0; i < kGridSamples; ++i) {
            const bool cornerColumn = (i == 0 || i == kLast) && (k == 0 || k == kLast);
            const bool alongZ = i == 0 || i == kLast;
            const bool even = cornerColumn || (alongZ ? k % 2 == 0 : i % 2 == 0);
            if (!even || !onStitchedSide(i, k)) {
                continue;
            }
            for (int j = 0; j < kGridSamples; j += 2) {
                grid.set(i, j, k, sample(i, j, k, originY, true));
            }
        }
    }
    return grid;
}

bool RockTile::onStitchedSide(int i, int k) const {
    const auto stitched = [this](unsigned seam) { return (seams_ & seam) != 0; };
    return (i == 0 && (stitched(kSeamNegX) || (k == 0 && stitched(kSeamNegXNegZ)) ||
                       (k == kLast && stitched(kSeamNegXPosZ)))) ||
           (i == kLast && (stitched(kSeamPosX) || (k == 0 && stitched(kSeamPosXNegZ)) ||
                           (k == kLast && stitched(kSeamPosXPosZ)))) ||
           (k == 0 && stitched(kSeamNegZ)) || (k == kLast && stitched(kSeamPosZ));
}

bool RockTile::onHorizonSide(int i, int k) const {
    return (i == 0 && (horizon_ & kSeamNegX) != 0) || (i == kLast && (horizon_ & kSeamPosX) != 0) ||
           (k == 0 && (horizon_ & kSeamNegZ) != 0) || (k == kLast && (horizon_ & kSeamPosZ) != 0);
}

void RockTile::dropPlants(std::vector<Plant>& plants) const {
    const double spacing = lattice_.spacing;
    std::erase_if(plants, [&](const Plant& plant) {
        const auto i = static_cast<int>(std::lround((plant.x - lattice_.x0) / spacing));
        const auto k = static_cast<int>(std::lround((plant.z - lattice_.z0) / spacing));
        return rockAt(std::clamp(i, 0, kLast), std::clamp(k, 0, kLast)).faceCount > 0;
    });
}

Vec3f RockTile::gradientAt(const DensityGrid& grid, int i, int j, int k, int originY) const {
    const float gx = derivative(i, [&](int n) { return grid.at(n, j, k); });
    const float gz = derivative(k, [&](int n) { return grid.at(i, j, n); });
    // Along y, centered through the rock beyond the level on its borders.
    const float below = j > 0 ? grid.at(i, j - 1, k) : sample(i, -1, k, originY, false);
    const float above =
        j < kLast ? grid.at(i, j + 1, k) : sample(i, kGridSamples, k, originY, false);
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
        rock.push_back(rockShare(rockAt(std::clamp(i, 0, kLast), std::clamp(k, 0, kLast)),
                                 originY + static_cast<double>(v.y)));
    }
    return rock;
}

} // namespace terrain::tile
