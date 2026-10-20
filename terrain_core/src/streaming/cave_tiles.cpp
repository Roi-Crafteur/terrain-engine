#include "terrain/streaming/cave_tiles.hpp"

#include "cave_tile.hpp"

#include <algorithm>

namespace terrain {

namespace {

// Horizontal margin around an entrance where the plants are dropped.
constexpr double kPlantEntranceMargin = 2.0;

constexpr int kLast = kGridSamples - 1;

// Box of the samples of the levels of a tile.
CaveBox boxOf(const tile::TileLattice& lattice, const std::vector<int>& levels) {
    const double side = lattice.levelHeight;
    return {static_cast<double>(lattice.x0), levels.front() * side,
            static_cast<double>(lattice.z0), lattice.x0 + side,
            (levels.back() + 1) * side, lattice.z0 + side};
}

} // namespace

CaveDetail caveDetailForLod(int lod) {
    const auto guard = static_cast<float>(3.0 * tileCellMeters(lod) + 1.0);
    switch (lod) {
    case 0:
        return {kAllCaveComponents, guard};
    case 1:
        return {kAllCaveComponents & ~static_cast<unsigned>(kCaveNoodles), guard};
    case 2:
        return {kCaveCheese | kCaveEntrances, guard};
    default:
        return {0, guard};
    }
}

namespace tile {

double caveDepthReach(const CaveField& field, const TileLattice& lattice) {
    const CaveParams& params = field.params();
    return static_cast<double>(params.maxDepth + params.blend) + lattice.spacing;
}

void dropPlantsOverEntrances(const CaveField& field, const CaveGround& ground,
                             const TileLattice& lattice, std::vector<Plant>& plants) {
    const double side = lattice.levelHeight;
    const std::vector<CaveEntrance> entrances =
        field.entrancesNear(lattice.x0, lattice.z0, lattice.x0 + side, lattice.z0 + side, ground);
    if (entrances.empty()) {
        return;
    }
    std::erase_if(plants, [&entrances](const Plant& plant) {
        return CaveField::nearEntrance(plant.x, plant.z, entrances, kPlantEntranceMargin);
    });
}

CaveTile::CaveTile(const CaveField& field, const CaveGround& ground, const TileRequest& request,
                   const TileLattice& lattice, const TileColumns& columns,
                   const std::vector<int>& levels)
    : lattice_(lattice), columns_(&columns), seams_(request.seams),
      fine_(carving(field, ground, caveDetailForLod(request.key.lod), boxOf(lattice, levels))) {
    const int lod = request.key.lod;
    if (request.seams != 0 && lod < kMaxLod) {
        const CaveDetail coarse = caveDetailForLod(lod + 1);
        if (!(coarse == fine_.sampler.detail())) {
            coarse_.emplace(carving(field, ground, coarse, boxOf(lattice, levels)));
        }
    }
}

CaveTile::Carving CaveTile::carving(const CaveField& field, const CaveGround& ground,
                                    const CaveDetail& detail, const CaveBox& box) const {
    Carving result{CaveSampler(field, ground, detail, box), {}};
    result.columns.reserve(static_cast<std::size_t>(kGridSamples * kGridSamples));
    for (int k = 0; k < kGridSamples; ++k) {
        for (int i = 0; i < kGridSamples; ++i) {
            result.columns.push_back(result.sampler.column(lattice_.x0 + i * lattice_.spacing,
                                                           lattice_.z0 + k * lattice_.spacing,
                                                           columns_->at(i, k)));
        }
    }
    return result;
}

float CaveTile::sample(Carving& carving, int i, int j, int k, int originY) const {
    const double sampleY = originY + j * lattice_.spacing;
    // Same terrain density as tile::levelDensity and TerrainTiles::densityAt.
    const auto terrain = static_cast<float>(sampleY - columns_->at(i, k));
    const CaveColumn& column = carving.columns[static_cast<std::size_t>(k * kGridSamples + i)];
    return carving.sampler.density(column, sampleY, terrain);
}

DensityGrid CaveTile::levelDensity(int originY) {
    DensityGrid grid(0.0f);
    for (int k = 0; k < kGridSamples; ++k) {
        for (int j = 0; j < kGridSamples; ++j) {
            for (int i = 0; i < kGridSamples; ++i) {
                grid.set(i, j, k, sample(fine_, i, j, k, originY));
            }
        }
    }
    if (coarse_) {
        resampleSeams(grid, originY);
    }
    return grid;
}

void CaveTile::resampleSeams(DensityGrid& grid, int originY) {
    const auto stitched = [this](unsigned seam) { return (seams_ & seam) != 0; };
    const auto onSide = [&](int i, int k) {
        const bool faceX = (i == 0 && stitched(kSeamNegX)) || (i == kLast && stitched(kSeamPosX));
        const bool faceZ = (k == 0 && stitched(kSeamNegZ)) || (k == kLast && stitched(kSeamPosZ));
        const bool corner = (i == 0 && k == 0 && stitched(kSeamNegXNegZ)) ||
                            (i == kLast && k == 0 && stitched(kSeamPosXNegZ)) ||
                            (i == 0 && k == kLast && stitched(kSeamNegXPosZ)) ||
                            (i == kLast && k == kLast && stitched(kSeamPosXPosZ));
        // The coarser tile samples the even lattice points of the side.
        return (faceX && k % 2 == 0) || (faceZ && i % 2 == 0) || corner;
    };
    for (int k = 0; k < kGridSamples; ++k) {
        for (int i = 0; i < kGridSamples; ++i) {
            if (!onSide(i, k)) {
                continue;
            }
            for (int j = 0; j < kGridSamples; j += 2) {
                grid.set(i, j, k, sample(*coarse_, i, j, k, originY));
            }
        }
    }
}

} // namespace tile

} // namespace terrain
