#include "terrain/generation/ecology.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace terrain {

namespace {

// Streams of the climate and the vegetation, distinct from the relief layers.
constexpr std::uint64_t kClimateStream = 0x434C494D;    // "CLIM"
constexpr std::uint64_t kVegetationStream = 0x56454745; // "VEGE"
constexpr std::uint64_t kBlendStream = 0x424C4E44;      // "BLND"

double sampleCoordinate(double center, int index, const HeightmapRegion& region) {
    return center + (index + 0.5 - region.size / 2.0) * region.metersPerPixel;
}

// Sample index i of the samples around coordinate (between i and i + 1) and the weight u of
// sample i + 1. The index is corrected after the division so that the weight is measured from
// the sample itself: regions whose samples coincide give exactly the same weights.
bool locate(double coordinate, double center, const HeightmapRegion& region, int& index,
            double& weight) {
    const double cells = (coordinate - center) / region.metersPerPixel + region.size / 2.0 - 0.5;
    index = static_cast<int>(std::floor(cells));
    weight = (coordinate - sampleCoordinate(center, index, region)) / region.metersPerPixel;
    if (weight < 0.0) {
        --index;
    } else if (weight >= 1.0) {
        ++index;
    }
    weight = (coordinate - sampleCoordinate(center, index, region)) / region.metersPerPixel;
    return index >= 0 && index <= region.size - 2;
}

std::int64_t firstCell(double coordinate, double cellSize) {
    return static_cast<std::int64_t>(std::ceil(coordinate / cellSize));
}

} // namespace

double EcologyMap::sampleX(int column) const {
    return sampleCoordinate(region.centerX, column, region);
}

double EcologyMap::sampleZ(int row) const {
    return sampleCoordinate(region.centerZ, row, region);
}

Ecology::Ecology(std::uint64_t seed, const EcologyParams& params, const WorldSize& world,
                 std::shared_ptr<const HumiditySource> humidity)
    : params_(params),
      climate_(noise::deriveSeed(seed, kClimateStream), params.climate, world, std::move(humidity)),
      vegetation_(noise::deriveSeed(seed, kVegetationStream), params.vegetation, world),
      blend_(noise::deriveSeed(seed, kBlendStream), params.blend, world) {}

EcologySample Ecology::sampleAt(double x, double z, const SurfaceSite& site) const {
    EcologySample sample;
    sample.climate = climate_.sample(x, z, site);
    sample.biome = classifyBiome(sample.climate, site, params_.biomes);
    sample.material = surfaceMaterial(sample.biome, sample.climate, site, params_.materials);
    sample.densities =
        vegetation_.densities(x, z, sample.biome, sample.material, sample.climate, site);
    return sample;
}

EcologyMap Ecology::map(const TerrainSurface& surface) const {
    EcologyMap result{surface.region, {}};
    result.samples.reserve(surface.sites.size());
    for (int row = 0; row < surface.size(); ++row) {
        const double z = surface.sampleZ(row);
        for (int column = 0; column < surface.size(); ++column) {
            result.samples.push_back(sampleAt(surface.sampleX(column), z, surface.at(column, row)));
        }
    }
    return result;
}

SurfaceBlend Ecology::blendAt(double x, double z, const SurfaceSite& site,
                              const ClimateSample& climate) const {
    return blend_.blend(x, z, climate, site, params_.biomes, params_.materials);
}

std::vector<SurfaceBlend> Ecology::blendMap(const TerrainSurface& surface,
                                            const EcologyMap& map) const {
    std::vector<SurfaceBlend> blends;
    blends.reserve(surface.sites.size());
    for (int row = 0; row < surface.size(); ++row) {
        const double z = surface.sampleZ(row);
        for (int column = 0; column < surface.size(); ++column) {
            blends.push_back(blendAt(surface.sampleX(column), z, surface.at(column, row),
                                     map.at(column, row).climate));
        }
    }
    return blends;
}

bool Ecology::densitiesAt(const EcologyMap& map, double x, double z,
                          PlantDensities& densities) const {
    int column = 0;
    int row = 0;
    double u = 0.0;
    double v = 0.0;
    if (!locate(x, map.region.centerX, map.region, column, u) ||
        !locate(z, map.region.centerZ, map.region, row, v)) {
        return false;
    }
    const std::array<const PlantDensities*, 4> corners = {
        &map.at(column, row).densities, &map.at(column + 1, row).densities,
        &map.at(column, row + 1).densities, &map.at(column + 1, row + 1).densities};
    for (std::size_t t = 0; t < densities.size(); ++t) {
        const double d00 = (*corners[0])[t];
        const double d10 = (*corners[1])[t];
        const double d01 = (*corners[2])[t];
        const double d11 = (*corners[3])[t];
        if (std::min({d00, d10, d01, d11}) <= 0.0) {
            densities[t] = 0.0f;
            continue;
        }
        const double top = d00 * (1.0 - u) + d10 * u;
        const double bottom = d01 * (1.0 - u) + d11 * u;
        densities[t] = static_cast<float>(top * (1.0 - v) + bottom * v);
    }
    return true;
}

bool Ecology::keepPlant(const Plant& plant, const ReliefHeight& surface) const {
    const VegetationParams& params = params_.vegetation;
    if (plant.type == PlantType::Seaweed) {
        const float depth = kSeaLevel - plant.y;
        if (depth < params.minSeaweedDepth || depth > params.maxSeaweedDepth) {
            return false;
        }
    } else if (plant.y < kSeaLevel) {
        return false;
    }
    const double step = params.probeStep;
    const double maxRise = params.rules[static_cast<std::size_t>(plant.type)].maxSlope * step;
    constexpr std::array<std::array<double, 2>, 4> kProbes = {
        {{1.0, 0.0}, {-1.0, 0.0}, {0.0, 1.0}, {0.0, -1.0}}};
    for (const auto& [dx, dz] : kProbes) {
        const float height = surface(plant.x + dx * step, plant.z + dz * step);
        if (std::abs(static_cast<double>(height) - plant.y) > maxRise) {
            return false;
        }
    }
    return true;
}

std::vector<Plant> Ecology::placePlants(const EcologyMap& map, const ReliefHeight& surface,
                                        const PlantArea& area) const {
    const double sizeX = vegetation_.cellsX().size;
    const double sizeZ = vegetation_.cellsZ().size;
    const std::int64_t firstI = firstCell(area.minX, sizeX);
    const std::int64_t lastI = firstCell(area.maxX, sizeX);
    const std::int64_t firstK = firstCell(area.minZ, sizeZ);
    const std::int64_t lastK = firstCell(area.maxZ, sizeZ);
    std::vector<Plant> plants;
    for (std::int64_t k = firstK; k < lastK; ++k) {
        for (std::int64_t i = firstI; i < lastI; ++i) {
            const LatticeDraw draw = vegetation_.drawCell(i, k);
            PlantDensities densities{};
            PlantType type = PlantType::Tree;
            if (!densitiesAt(map, draw.x, draw.z, densities) ||
                !pickPlant(densities, draw.pick, type)) {
                continue;
            }
            const Plant plant{type,       draw.x,       draw.z, surface(draw.x, draw.z),
                              draw.scale, draw.rotation};
            if (keepPlant(plant, surface)) {
                plants.push_back(plant);
            }
        }
    }
    return plants;
}

} // namespace terrain
