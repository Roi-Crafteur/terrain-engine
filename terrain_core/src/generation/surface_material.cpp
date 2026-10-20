#include "terrain/generation/surface_material.hpp"

#include <algorithm>
#include <array>

namespace terrain {

namespace {

constexpr std::array<const char*, kSurfaceMaterialCount> kMaterialNames = {
    "water", "sand", "grass", "dirt", "rock", "snow", "mud", "pebbles",
};

// Material of the biomes whose ground is mostly mineral: snow, mountain, beach and desert.
// Returns false for the other biomes.
bool mineralMaterial(Biome biome, const ClimateSample& climate, float steepest,
                     const MaterialParams& params, SurfaceMaterial& material) {
    switch (biome) {
    case Biome::Snow:
        material = SurfaceMaterial::Snow;
        return true;
    case Biome::Mountain:
        if (climate.temperature <= params.mountainSnowTemperature) {
            material = SurfaceMaterial::Snow;
        } else {
            material =
                steepest >= params.screeSlope ? SurfaceMaterial::Pebbles : SurfaceMaterial::Rock;
        }
        return true;
    case Biome::Beach:
        material = climate.temperature < params.pebbleBeachTemperature ? SurfaceMaterial::Pebbles
                                                                       : SurfaceMaterial::Sand;
        return true;
    case Biome::Desert:
        material = SurfaceMaterial::Sand;
        return true;
    default:
        return false;
    }
}

} // namespace

const char* materialName(SurfaceMaterial material) {
    return kMaterialNames[static_cast<std::size_t>(material)];
}

float steepestSlope(const ClimateSample& climate, const SurfaceSite& site) {
    return std::max(climate.slope, site.steepness);
}

SurfaceMaterial surfaceMaterial(Biome biome, const ClimateSample& climate, const SurfaceSite& site,
                                const MaterialParams& params) {
    if (!site.dry()) {
        return SurfaceMaterial::Water;
    }
    const float steepest = steepestSlope(climate, site);
    if (steepest >= params.rockSlope || site.cliffWall || site.rockAboveAir) {
        return SurfaceMaterial::Rock;
    }
    SurfaceMaterial material = SurfaceMaterial::Grass;
    if (mineralMaterial(biome, climate, steepest, params, material)) {
        return material;
    }
    if (biome == Biome::Marsh) {
        return SurfaceMaterial::Mud;
    }
    if (site.riverDistance <= params.riverBankWidth) {
        return SurfaceMaterial::Pebbles;
    }
    if (biome == Biome::Tundra || steepest >= params.dirtSlope) {
        return SurfaceMaterial::Dirt;
    }
    return SurfaceMaterial::Grass;
}

} // namespace terrain
