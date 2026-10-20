#include "terrain/generation/biome.hpp"

#include <array>

namespace terrain {

namespace {

constexpr std::array<const char*, kBiomeCount> kBiomeNames = {
    "ocean",           "beach", "desert", "savanna",  "grassland", "temperate forest",
    "tropical forest", "taiga", "tundra", "mountain", "snow",      "marsh",
};

bool isSnow(const ClimateSample& climate, const BiomeParams& params) {
    return climate.temperature <= params.snowTemperature && climate.slope <= params.snowMaxSlope;
}

// The walls of the cliffs are steep at the scale of a sample only: they keep the biome of the
// land around them (their material is rock anyway).
bool isMountain(const ClimateSample& climate, const SurfaceSite& site, float altitude,
                const BiomeParams& params) {
    return altitude >= params.mountainMinAltitude &&
           (climate.temperature <= params.treelineTemperature ||
            (climate.slope >= params.mountainSlope && !site.cliffWall));
}

bool isBeach(const ClimateSample& climate, float altitude, const BiomeParams& params) {
    return altitude <= params.beachMaxHeight && climate.slope <= params.beachMaxSlope;
}

bool isMarsh(const ClimateSample& climate, const SurfaceSite& site, float altitude,
             const BiomeParams& params) {
    const bool wet =
        site.riverDistance <= params.marshRiverReach || altitude <= params.marshCoastHeight;
    return wet && climate.slope <= params.marshMaxSlope && altitude <= params.marshMaxAltitude &&
           climate.humidity >= params.marshMinHumidity &&
           climate.temperature >= params.marshMinTemperature;
}

} // namespace

const char* biomeName(Biome biome) {
    return kBiomeNames[static_cast<std::size_t>(biome)];
}

Biome whittakerBiome(float temperature, float humidity, const BiomeParams& params) {
    if (temperature < params.tundraTemperature) {
        return Biome::Tundra;
    }
    if (temperature < params.taigaTemperature) {
        return humidity < params.taigaMinHumidity ? Biome::Tundra : Biome::Taiga;
    }
    if (temperature < params.tropicalTemperature) {
        if (humidity < params.temperateDesertHumidity) {
            return Biome::Desert;
        }
        return humidity < params.forestHumidity ? Biome::Grassland : Biome::TemperateForest;
    }
    if (humidity < params.hotDesertHumidity) {
        return Biome::Desert;
    }
    return humidity < params.rainforestHumidity ? Biome::Savanna : Biome::TropicalForest;
}

Biome classifyBiome(const ClimateSample& climate, const SurfaceSite& site,
                    const BiomeParams& params) {
    if (site.water == WaterKind::Ocean) {
        return Biome::Ocean;
    }
    const float altitude = site.ground - kSeaLevel;
    if (isSnow(climate, params)) {
        return Biome::Snow;
    }
    if (isMountain(climate, site, altitude, params)) {
        return Biome::Mountain;
    }
    if (isBeach(climate, altitude, params)) {
        return Biome::Beach;
    }
    if (isMarsh(climate, site, altitude, params)) {
        return Biome::Marsh;
    }
    return whittakerBiome(climate.temperature, climate.humidity, params);
}

} // namespace terrain
