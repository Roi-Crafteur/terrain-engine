#include "terrain/streaming/cave_tiles.hpp"

#include "cave_tile.hpp"

#include <algorithm>
#include <cstddef>

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
        return {kCaveCheese | kCaveEntrances | kCaveGalleries, guard};
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
    // The coarser tile samples the even lattice points of the side: every other sample along
    // a face, every sample of a corner column (every other one vertically, as on the faces).
    const auto even = [](int i, int k) {
        const bool cornerColumn = (i == 0 || i == kLast) && (k == 0 || k == kLast);
        const bool alongZ = i == 0 || i == kLast;
        return cornerColumn || (alongZ ? k % 2 == 0 : i % 2 == 0);
    };
    for (int k = 0; k < kGridSamples; ++k) {
        for (int i = 0; i < kGridSamples; ++i) {
            if (!onStitchedSide(i, k) || !even(i, k)) {
                continue;
            }
            for (int j = 0; j < kGridSamples; j += 2) {
                grid.set(i, j, k, sample(*coarse_, i, j, k, originY));
            }
        }
    }
}

bool CaveTile::onStitchedSide(int i, int k) const {
    const auto stitched = [this](unsigned seam) { return (seams_ & seam) != 0; };
    return (i == 0 && (stitched(kSeamNegX) || (k == 0 && stitched(kSeamNegXNegZ)) ||
                       (k == kLast && stitched(kSeamNegXPosZ)))) ||
           (i == kLast && (stitched(kSeamPosX) || (k == 0 && stitched(kSeamPosXNegZ)) ||
                           (k == kLast && stitched(kSeamPosXPosZ)))) ||
           (k == 0 && stitched(kSeamNegZ)) || (k == kLast && stitched(kSeamPosZ));
}

bool CaveTile::holdsWater() const {
    return fine_.sampler.detail().has(kCaveAquifers);
}

DensityGrid CaveTile::fluidDensity(CaveFluid fluid, const DensityGrid& carved, int originY) {
    const float none = fine_.sampler.field().params().voidClamp;
    const bool coarseWater = !coarse_ || coarse_->sampler.detail().has(kCaveAquifers);
    DensityGrid grid(none);
    for (int k = 0; k < kGridSamples; ++k) {
        for (int i = 0; i < kGridSamples; ++i) {
            if (!coarseWater && onStitchedSide(i, k)) {
                continue;
            }
            const auto index = static_cast<std::size_t>(k * kGridSamples + i);
            const CaveColumn& column = fine_.columns[index];
            for (int j = 0; j < kGridSamples; ++j) {
                const double sampleY = originY + j * lattice_.spacing;
                const auto terrain = static_cast<float>(sampleY - columns_->at(i, k));
                const float air = carved.at(i, j, k);
                grid.set(i, j, k,
                         fluid == CaveFluid::Water
                             ? fine_.sampler.waterDensity(column, sampleY, terrain, air)
                             : fine_.sampler.lavaDensity(column, sampleY, terrain, air));
            }
        }
    }
    if (coarseWater) {
        stitchSeams(grid, seams_);
    }
    return grid;
}

float CaveTile::groundAt(double x, double z) const {
    const double u = std::clamp((x - lattice_.x0) / lattice_.spacing, 0.0, double{kChunkCells});
    const double v = std::clamp((z - lattice_.z0) / lattice_.spacing, 0.0, double{kChunkCells});
    const int i = std::min(static_cast<int>(u), kChunkCells - 1);
    const int k = std::min(static_cast<int>(v), kChunkCells - 1);
    const double fu = u - i;
    const double fv = v - k;
    const auto at = [this](int a, int b) { return static_cast<double>(columns_->at(a, b)); };
    const double low = at(i, k) + fu * (at(i + 1, k) - at(i, k));
    const double high = at(i, k + 1) + fu * (at(i + 1, k + 1) - at(i, k + 1));
    return static_cast<float>(low + fv * (high - low));
}

void CaveTile::paint(TileData& data, const CaveEnvironmentField& environment,
                     const CaveMaterialParams& params) {
    CaveEnvironmentCache environments(environment);
    data.materials.reserve(data.mesh.vertices.size());
    for (std::size_t n = 0; n < data.mesh.vertices.size(); ++n) {
        const Vec3f v = data.mesh.vertices[n];
        const double x = data.originX + v.x;
        const double y = data.originY + v.y;
        const double z = data.originZ + v.z;
        CaveWallSite site;
        site.depth = static_cast<float>(static_cast<double>(groundAt(x, z)) - y);
        if (site.depth >= params.surfaceDepth) {
            const AquiferCell& aquifer = fine_.sampler.aquiferAt(x, z);
            site.environment = environments.at(x, z);
            site.normalY = data.mesh.normals[n].y;
            const CaveProfile profile = fine_.sampler.profileAt(x, z);
            site.icyRegion = profile.icy();
            site.lushRegion = profile.lush();
            site.volcanicRegion = profile.volcanic();
            if (aquifer.wet && holdsWater()) {
                site.aboveWater = static_cast<float>(y - static_cast<double>(aquifer.level));
                site.frozenWater = aquifer.frozen;
            }
            if (aquifer.lava && holdsWater()) {
                site.aboveLava = static_cast<float>(y - static_cast<double>(aquifer.lavaLevel));
            }
        }
        data.materials.push_back(caveMaterial(site, params));
    }
    data.waterMaterials.reserve(data.water.vertices.size());
    for (const Vec3f& v : data.water.vertices) {
        const bool frozen = fine_.sampler.aquiferAt(data.originX + v.x, data.originZ + v.z).frozen;
        data.waterMaterials.push_back(frozen ? CaveMaterial::Ice : CaveMaterial::Water);
    }
}

void CaveTile::decorate(TileData& data, const CaveEnvironmentField& environment,
                        const CaveMaterialParams& params, const SpeleothemField* speleothems,
                        const CaveFloraField* flora) {
    paint(data, environment, params);
    const double x1 = lattice_.x0 + lattice_.levelHeight;
    const double z1 = lattice_.z0 + lattice_.levelHeight;
    if (speleothems != nullptr) {
        data.speleothems = speleothems->place(fine_.sampler, lattice_.x0, lattice_.z0, x1, z1);
    }
    if (flora != nullptr) {
        data.caveFlora = flora->place(fine_.sampler, lattice_.x0, lattice_.z0, x1, z1);
    }
}

} // namespace tile

} // namespace terrain
