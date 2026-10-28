#include "rock_tile.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace terrain::tile {

namespace {

constexpr int kLast = kGridSamples - 1;

} // namespace

RockTile::RockTile(const TileColumns& columns, const TileRequest& request,
                   const TileLattice& lattice)
    : columns_(&columns), lattice_(lattice), seams_(request.seams),
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
    if (c.faceCount == 0) {
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

void RockTile::dropPlants(std::vector<Plant>& plants) const {
    const double spacing = lattice_.spacing;
    std::erase_if(plants, [&](const Plant& plant) {
        const auto i = static_cast<int>(std::lround((plant.x - lattice_.x0) / spacing));
        const auto k = static_cast<int>(std::lround((plant.z - lattice_.z0) / spacing));
        return rockAt(std::clamp(i, 0, kLast), std::clamp(k, 0, kLast)).faceCount > 0;
    });
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
