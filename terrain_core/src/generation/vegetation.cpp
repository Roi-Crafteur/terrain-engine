#include "terrain/generation/vegetation.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cstddef>
#include <numbers>

namespace terrain {

namespace {

constexpr std::array<const char*, kPlantTypeCount> kPlantNames = {
    "tree", "bush", "flower", "tall grass", "cactus", "boulder", "seaweed",
};

// New layers go at the end so that the seeds of the existing ones never change.
enum Layer : std::uint64_t { kClump, kPlacement };

// Draws of a lattice cell, each from its own stream of the cell seed.
enum Draw : std::uint64_t { kDrawX, kDrawZ, kDrawPick, kDrawScale, kDrawRotation };

// Uniform value in [0, 1) from the 53 high bits of a hash.
double unit(std::uint64_t hash) {
    return static_cast<double>(hash >> 11U) * 0x1.0p-53;
}

std::size_t slot(PlantType type) {
    return static_cast<std::size_t>(type);
}

} // namespace

const char* plantName(PlantType type) {
    return kPlantNames[slot(type)];
}

VegetationField::VegetationField(std::uint64_t seed, const VegetationParams& params)
    : params_(params), clumpSeed_(noise::deriveSeed(seed, kClump)),
      placementSeed_(noise::deriveSeed(seed, kPlacement)) {}

float VegetationField::clumpAt(double x, double z) const {
    const noise::FbmParams fbm{.octaves = 3, .frequency = 1.0 / params_.clumpWavelength};
    return std::clamp(0.5f + noise::fbm2D(x, z, clumpSeed_, fbm), 0.0f, 1.0f);
}

bool VegetationField::canGrow(PlantType type, SurfaceMaterial material,
                              const ClimateSample& climate, const SurfaceSite& site) const {
    const PlantRule& rule = params_.rules[slot(type)];
    if ((rule.materials & materialBit(material)) == 0) {
        return false;
    }
    if (type == PlantType::Seaweed) {
        const float depth = site.waterDepth();
        return site.water == WaterKind::Ocean && depth >= params_.minSeaweedDepth &&
               depth <= params_.maxSeaweedDepth && climate.slope <= rule.maxSlope;
    }
    return site.dry() && !site.rockAboveAir && !site.cliffWall &&
           steepestSlope(climate, site) <= rule.maxSlope;
}

PlantDensities VegetationField::densities(double x, double z, Biome biome, SurfaceMaterial material,
                                          const ClimateSample& climate,
                                          const SurfaceSite& site) const {
    const PlantDensities& base = params_.biomeDensity[static_cast<std::size_t>(biome)];
    const float woods = params_.clumpContrast * (2.0f * clumpAt(x, z) - 1.0f);
    const float steepest = steepestSlope(climate, site);
    PlantDensities result{};
    float total = 0.0f;
    for (int t = 0; t < kPlantTypeCount; ++t) {
        const auto type = static_cast<PlantType>(t);
        const std::size_t i = slot(type);
        if (base[i] <= 0.0f || !canGrow(type, material, climate, site)) {
            continue;
        }
        float density = base[i];
        if (type == PlantType::Tree || type == PlantType::Bush) {
            density *= 1.0f + woods;
        } else if (type == PlantType::Flower || type == PlantType::TallGrass) {
            density *= 1.0f - woods;
        }
        if (type != PlantType::Seaweed) {
            const float maxSlope = params_.rules[i].maxSlope;
            density *= 1.0f - noise::smoothstep(0.5f * maxSlope, maxSlope, steepest);
        }
        result[i] = density;
        total += density;
    }
    if (total > 1.0f) {
        for (float& density : result) {
            density /= total;
        }
    }
    return result;
}

LatticeDraw VegetationField::drawCell(std::int64_t i, std::int64_t k) const {
    const std::uint64_t cell =
        noise::deriveSeed(noise::deriveSeed(placementSeed_, static_cast<std::uint64_t>(i)),
                          static_cast<std::uint64_t>(k));
    const auto draw = [cell](Draw stream) { return unit(noise::deriveSeed(cell, stream)); };
    const double size = params_.cellSize;
    LatticeDraw result;
    result.x = (static_cast<double>(i) + draw(kDrawX)) * size;
    result.z = (static_cast<double>(k) + draw(kDrawZ)) * size;
    result.pick = draw(kDrawPick);
    result.scale = params_.minScale +
                   (params_.maxScale - params_.minScale) * static_cast<float>(draw(kDrawScale));
    result.rotation = static_cast<float>(2.0 * std::numbers::pi * draw(kDrawRotation));
    return result;
}

bool pickPlant(const PlantDensities& densities, double pick, PlantType& type) {
    double cumulative = 0.0;
    for (int t = 0; t < kPlantTypeCount; ++t) {
        cumulative += densities[static_cast<std::size_t>(t)];
        if (densities[static_cast<std::size_t>(t)] > 0.0f && pick < cumulative) {
            type = static_cast<PlantType>(t);
            return true;
        }
    }
    return false;
}

float totalDensity(const PlantDensities& densities) {
    float total = 0.0f;
    for (const float density : densities) {
        total += density;
    }
    return total;
}

} // namespace terrain
