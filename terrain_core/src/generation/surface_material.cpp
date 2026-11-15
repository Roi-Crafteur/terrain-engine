#include "terrain/generation/surface_material.hpp"

#include <algorithm>
#include <array>

namespace terrain {

namespace {

constexpr std::array<const char*, kSurfaceMaterialCount> kMaterialNames = {
    "water", "sand", "grass", "dirt", "rock", "snow", "mud", "pebbles", "gravel", "stone", "crust",
};

// The ground of a desert: near a river, pebbles on its banks, dirt on steeper ground and grass
// elsewhere; away from it, its sand, stone or gravel.
SurfaceMaterial desertMaterial(Biome biome, const SurfaceSite& site, float steepest,
                               const MaterialParams& params) {
    if (site.riverDistance <= params.desertGrassReach) {
        if (site.riverDistance <= params.riverBankWidth) {
            return SurfaceMaterial::Pebbles;
        }
        return steepest >= params.dirtSlope ? SurfaceMaterial::Dirt : SurfaceMaterial::Grass;
    }
    if (biome == Biome::SandDesert) {
        return SurfaceMaterial::Sand;
    }
    return site.ground - kSeaLevel >= params.hamadaAltitude ? SurfaceMaterial::Stone
                                                            : SurfaceMaterial::Gravel;
}

// Material of the biomes whose ground is mostly mineral: snow, mountain, beach, the polar region
// and the deserts. Returns false for the other biomes.
bool mineralMaterial(Biome biome, const ClimateSample& climate, const SurfaceSite& site,
                     float steepest, const MaterialParams& params, SurfaceMaterial& material) {
    switch (biome) {
    case Biome::Snow:
    case Biome::IceDesert:
        material = SurfaceMaterial::Snow;
        return true;
    case Biome::Mountain:
    case Biome::HighMountain:
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
    case Biome::Polar:
        material = SurfaceMaterial::Gravel;
        return true;
    case Biome::RockyDesert:
    case Biome::SandDesert:
    case Biome::ColdDesert:
        material = desertMaterial(biome, site, steepest, params);
        return true;
    default:
        return false;
    }
}

} // namespace

SurfaceMaterial ergFloorMaterial(float kind) {
    if (kind < 0.5f) {
        return SurfaceMaterial::Sand;
    }
    return kind <= 1.5f ? SurfaceMaterial::Gravel : SurfaceMaterial::Crust;
}

const char* materialName(SurfaceMaterial material) {
    return kMaterialNames[static_cast<std::size_t>(material)];
}

float steepestSlope(const ClimateSample& climate, const SurfaceSite& site) {
    return std::max(climate.slope, site.steepness);
}

SurfaceMaterial surfaceMaterial(Biome biome, const ClimateSample& climate, const SurfaceSite& site,
                                const MaterialParams& params) {
    if (site.bareRock) {
        return SurfaceMaterial::Rock;
    }
    if (!site.dry()) {
        return SurfaceMaterial::Water;
    }
    // The dunes of the ergs are sand whatever their slope and their biome: the core lays them
    // (ergs.hpp); only the walls of the cliffs and the rock above air stay rock.
    if (site.dunes >= params.duneSand && !site.cliffWall && !site.rockAboveAir) {
        return SurfaceMaterial::Sand;
    }
    const float steepest = steepestSlope(climate, site);
    if (steepest >= params.rockSlope || site.cliffWall || site.rockAboveAir) {
        return SurfaceMaterial::Rock;
    }
    if (site.erg >= params.ergFloorErg && site.riverDistance > params.ergGrassReach) {
        return ergFloorMaterial(site.ergFloor);
    }
    SurfaceMaterial material = SurfaceMaterial::Grass;
    if (mineralMaterial(biome, climate, site, steepest, params, material)) {
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
