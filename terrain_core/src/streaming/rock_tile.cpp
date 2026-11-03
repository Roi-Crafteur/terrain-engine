#include "rock_tile.hpp"

#include "terrain/meshing/marching_cubes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace terrain::tile {

namespace {

constexpr int kLast = kGridSamples - 1;
// On a side stitched to a coarser tile, the density of the rock turns from the tile's detail to
// the coarser one's over kSeamBlend cells: the faces the coarser tile does not draw fade out, not
// cut short at the side (over half as many cells, such a face ends there in a narrow dark flank).
constexpr double kSeamBlend = 8.0;

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
        // Up to the top of its rock only: no level of air above it (rockCeiling: the tops of the
        // carved faces, of the valleys, never raise the rock).
        const RockBounds b = rockBounds(c);
        bounds_.low = std::min(bounds_.low, b.low);
        bounds_.high = std::max(bounds_.high, rockCeiling(c));
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
    if (!c.hasRock() || onHorizonSide(i, k)) {
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
        const double ground = static_cast<double>(columns_->at(std::clamp(i, 0, kLast),
                                                               std::clamp(k, 0, kLast)));
        return std::any_of(c.faces.begin(), c.faces.begin() + c.faceCount,
                           [this](const RockFace& f) { return drawnWeight(f, fine_) > 0.0f; }) ||
               (c.spire.radius > 0.0f && rockDensity(c, ground + 0.5, fine_) < 0.0f);
    });
}

float RockTile::valueAt(const DensityGrid& grid, int i, int j, int k, int originY) const {
    if (i >= 0 && i <= kLast && j >= 0 && j <= kLast && k >= 0 && k <= kLast) {
        return grid.at(i, j, k);
    }
    return sampleBlended(i, j, k, originY, fineShare(i, k));
}

Vec3f RockTile::gradientAt(const DensityGrid& grid, int i, int j, int k, int originY) const {
    const bool ring = hasRing();
    const float gx = derivative(i, ring, [&](int n) { return valueAt(grid, n, j, k, originY); });
    const float gz = derivative(k, ring, [&](int n) { return valueAt(grid, i, j, n, originY); });
    // Along y, centered through the rock beyond the level on its borders.
    const float below = valueAt(grid, i, j - 1, k, originY);
    const float above = valueAt(grid, i, j + 1, k, originY);
    return {gx, 0.5f * (above - below), gz};
}

void RockTile::lightLevel(Mesh& level, const DensityGrid& grid, int originY) const {
    // The gradients of the lattice points from one row under the level to one over it, each found
    // once, and averaged with those above and below.
    constexpr int kRows = kGridSamples + 2;
    std::vector<Vec3f> gradients(static_cast<std::size_t>(kGridSamples * kRows * kGridSamples));
    std::vector<bool> found(gradients.size(), false);
    const auto gradient = [&](int i, int j, int k) {
        const auto at = static_cast<std::size_t>((k * kRows + j + 1) * kGridSamples + i);
        if (!found[at]) {
            gradients[at] = gradientAt(grid, i, j, k, originY);
            found[at] = true;
        }
        return gradients[at];
    };
    const auto smoothed = [&](int i, int j, int k) {
        const Vec3f below = gradient(i, j - 1, k);
        const Vec3f here = gradient(i, j, k);
        const Vec3f above = gradient(i, j + 1, k);
        return Vec3f{(below.x + here.x + above.x) / 3.0f, (below.y + here.y + above.y) / 3.0f,
                     (below.z + here.z + above.z) / 3.0f};
    };
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
        Vec3f g = smoothed(low[0], low[1], low[2]);
        if (axis >= 0) {
            std::array<int, 3> high = low;
            ++high[static_cast<std::size_t>(axis)];
            const Vec3f h = smoothed(high[0], high[1], high[2]);
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

std::vector<RockBands> RockTile::vertexBands(const Mesh& mesh, double originY) const {
    std::vector<RockBands> bands;
    bands.reserve(mesh.vertices.size());
    const auto spacing = static_cast<float>(lattice_.spacing);
    for (const Vec3f& v : mesh.vertices) {
        const auto i = static_cast<int>(std::lround(v.x / spacing));
        const auto k = static_cast<int>(std::lround(v.z / spacing));
        const RockColumn& c = rockAt(std::clamp(i, 0, kLast), std::clamp(k, 0, kLast));
        bands.push_back(rockBands(c, originY + static_cast<double>(v.y)));
    }
    return bands;
}

namespace {

// Down from far above a spire in strides (the density about the distance to it: kStride of it,
// whole meters) while kFar meters away at least.
constexpr double kFar = 3.0;
constexpr double kStride = 0.6;

} // namespace

float spireGroundAt(const RockColumn& column, double x, double z, const RockDetail& detail) {
    const double x0 = std::floor(x);
    const double z0 = std::floor(z);
    const double fx = x - x0;
    const double fz = z - z0;
    const std::array<double, 4> w{(1.0 - fx) * (1.0 - fz), fx * (1.0 - fz), (1.0 - fx) * fz,
                                  fx * fz};
    RockColumn relief = column;
    relief.spire = RockSpire{};
    const auto densityAt = [&](double y) {
        double spire = 0.0;
        for (std::size_t q = 0; q < w.size(); ++q) {
            if (w[q] > 0.0) {
                const double dx = static_cast<double>(column.spire.dx) + (x0 - x) +
                                  static_cast<double>(q & 1U);
                const double dz = static_cast<double>(column.spire.dz) + (z0 - z) +
                                  static_cast<double>(q >> 1U);
                spire += w[q] * static_cast<double>(spireDensity(column.spire, dx, dz, y, detail));
            }
        }
        return std::min(spire, static_cast<double>(rockDensity(relief, y, detail)));
    };
    const RockBounds bounds = rockBounds(column);
    const double low = static_cast<double>(bounds.low);
    // Down in strides while far from the rock (the density about the distance to it: kStride of
    // it, whole meters), then a meter at a time.
    double above = std::ceil(static_cast<double>(bounds.high)) + 1.0;
    double previous = densityAt(above);
    while (previous > kFar && above > low) {
        above -= std::floor(kStride * previous);
        previous = densityAt(above);
    }
    if (!(previous >= kIsoLevel)) {
        above += std::ceil(kFar);
        previous = densityAt(above);
    }
    for (double y = above - 1.0; y > low - 2.0; y -= 1.0) {
        const double d = densityAt(y);
        if (d < kIsoLevel && previous >= kIsoLevel) {
            return static_cast<float>(y + (kIsoLevel - d) / (previous - d));
        }
        previous = d;
    }
    return static_cast<float>(low);
}

} // namespace terrain::tile
