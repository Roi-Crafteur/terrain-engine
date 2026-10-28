#include "rock_tile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace terrain::tile {

namespace {

constexpr int kLast = kGridSamples - 1;
// A normal of the density keeps its vertex where it agrees with the triangles around it (its
// cosine with their normal at least kAgree): on a feature of the rock as thin as a cell, the
// differences over two cells may point it into the rock.
constexpr float kAgree = 0.3f;

// The normal of a triangle of a mesh (indices first..first + 2), its length twice its area.
Vec3f windingOf(const Mesh& mesh, std::size_t first) {
    const Vec3f& a = mesh.vertices[mesh.indices[first]];
    const Vec3f& b = mesh.vertices[mesh.indices[first + 1]];
    const Vec3f& c = mesh.vertices[mesh.indices[first + 2]];
    const Vec3f u{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vec3f v{c.x - a.x, c.y - a.y, c.z - a.z};
    return {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
}

float dot(const Vec3f& a, const Vec3f& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3f unit(const Vec3f& v) {
    const float length = std::sqrt(dot(v, v));
    return length > 0.0f ? Vec3f{v.x / length, v.y / length, v.z / length} : v;
}

// Turns the normals of the vertices of the triangles whose normals point against their winding
// (a fold of the surface as thin as a cell) just in front of them (kFront), a few times over:
// their neighbors share vertices with them.
void unfold(Mesh& mesh) {
    constexpr int kRounds = 8;
    constexpr float kFront = 0.1f;
    for (int round = 0; round < kRounds; ++round) {
        bool folded = false;
        for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            const Vec3f winding = windingOf(mesh, t);
            Vec3f sum{};
            for (std::size_t k = 0; k < 3; ++k) {
                const Vec3f& n = mesh.normals[mesh.indices[t + k]];
                sum = {sum.x + n.x, sum.y + n.y, sum.z + n.z};
            }
            if (!(dot(sum, winding) < 0.0f)) {
                continue;
            }
            folded = true;
            const Vec3f w = unit(winding);
            for (std::size_t k = 0; k < 3; ++k) {
                Vec3f& n = mesh.normals[mesh.indices[t + k]];
                const float facing = dot(n, w);
                if (facing < kFront) {
                    const float lift = kFront - facing;
                    n = unit({n.x + lift * w.x, n.y + lift * w.y, n.z + lift * w.z});
                }
            }
        }
        if (!folded) {
            return;
        }
    }
}

// The normals of the triangles of a mesh summed at their vertices, weighted by their areas.
std::vector<Vec3f> triangleNormals(const Mesh& mesh) {
    std::vector<Vec3f> sums(mesh.vertices.size());
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const Vec3f n = windingOf(mesh, t);
        for (std::size_t k = 0; k < 3; ++k) {
            Vec3f& sum = sums[mesh.indices[t + k]];
            sum = {sum.x + n.x, sum.y + n.y, sum.z + n.z};
        }
    }
    return sums;
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
    const double sampleY = originY + j * lattice_.spacing;
    const RockColumn& c = columnAt(i, k);
    if (c.faceCount == 0 || onHorizonSide(i, k)) {
        // Same expression as tile::levelDensity and TerrainTiles::densityAt.
        return static_cast<float>(sampleY - groundAt(i, k));
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
    const float gx = derivative(i, ring, [&](int n) {
        return n >= 0 && n <= kLast ? grid.at(n, j, k) : sample(n, j, k, originY, false);
    });
    const float gz = derivative(k, ring, [&](int n) {
        return n >= 0 && n <= kLast ? grid.at(i, j, n) : sample(i, j, n, originY, false);
    });
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
    const std::vector<Vec3f> around = triangleNormals(level);
    for (std::size_t n = 0; n < level.vertices.size(); ++n) {
        const Vec3f& a = around[n];
        const float length = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
        const Vec3f& g = level.normals[n];
        if (length > 0.0f && g.x * a.x + g.y * a.y + g.z * a.z < kAgree * length) {
            level.normals[n] = {a.x / length, a.y / length, a.z / length};
        }
    }
    unfold(level);
}

std::vector<float> RockTile::vertexRock(const Mesh& mesh, double originY) const {
    std::vector<float> rock;
    rock.reserve(mesh.vertices.size());
    const auto spacing = static_cast<float>(lattice_.spacing);
    for (const Vec3f& v : mesh.vertices) {
        const auto i = static_cast<int>(std::lround(v.x / spacing));
        const auto k = static_cast<int>(std::lround(v.z / spacing));
        rock.push_back(rockShare(rockAt(std::clamp(i, 0, kLast), std::clamp(k, 0, kLast)),
                                 originY + static_cast<double>(v.y), fine_));
    }
    return rock;
}

} // namespace terrain::tile
