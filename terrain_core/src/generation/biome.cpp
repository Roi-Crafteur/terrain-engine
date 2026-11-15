#include "terrain/generation/biome.hpp"

#include <array>

namespace terrain {

namespace {

constexpr std::array<const char*, kBiomeCount> kBiomeNames = {
    "ocean",       "beach",        "rocky desert",        "savanna",      "grassland",
    "temperate forest", "tropical forest", "taiga",       "tundra",       "mountain",
    "snow",        "marsh",        "dry forest",          "steppe",       "sand desert",
    "cold desert", "high mountain", "polar region",       "ice desert",   "garrigue and maquis",
    "shrubland",
};

// The polar region by its latitude: the base temperature (at sea level) cold enough.
bool polarCold(const ClimateSample& climate, const BiomeParams& params) {
    return climate.baseTemperature <= params.snowTemperature + params.polarBaseOffset;
}

// Below the snow: the polar region where it is too dry for ice, else snow on gentle ground (the
// ice desert where the cold comes from the latitude). Ocean if nothing (the caller goes on).
bool coldBiome(const ClimateSample& climate, const BiomeParams& params, Biome& biome) {
    if (climate.temperature > params.snowTemperature) {
        return false;
    }
    if (polarCold(climate, params) && climate.humidity < params.polarDryHumidity) {
        biome = Biome::Polar;
        return true;
    }
    if (climate.slope > params.snowMaxSlope) {
        return false;
    }
    const bool sheet =
        climate.baseTemperature <= params.snowTemperature + params.iceDesertBaseOffset;
    biome = sheet ? Biome::IceDesert : Biome::Snow;
    return true;
}

// The walls of the cliffs are steep at the scale of a sample only: they keep the biome of the
// land around them (their material is rock anyway).
bool isMountain(const ClimateSample& climate, const SurfaceSite& site, float altitude,
                const BiomeParams& params) {
    return altitude >= params.mountainMinAltitude &&
           (climate.temperature <= params.treelineTemperature ||
            (climate.slope >= params.mountainSlope && !site.cliffWall));
}

// Dry land below the sea level lies in a depression: never a beach; nor far from the sea.
bool isBeach(const ClimateSample& climate, const SurfaceSite& site, float altitude,
             const BiomeParams& params) {
    return altitude >= 0.0f && altitude <= params.beachMaxHeight &&
           climate.slope <= params.beachMaxSlope && coastDistance(site) <= params.beachCoastReach;
}

bool isMarsh(const ClimateSample& climate, const SurfaceSite& site, float altitude,
             const BiomeParams& params) {
    const bool coast = altitude <= params.marshCoastHeight &&
                       coastDistance(site) <= params.marshCoastReach;
    const bool wet = site.riverDistance <= params.marshRiverReach || coast;
    return wet && climate.slope <= params.marshMaxSlope && altitude <= params.marshMaxAltitude &&
           climate.humidity >= params.marshMinHumidity &&
           climate.temperature >= params.marshMinTemperature;
}

Biome temperateBiome(float temperature, float humidity, const BiomeParams& params) {
    if (humidity < params.temperateDesertHumidity) {
        return temperature < params.coldDesertTemperature ? Biome::ColdDesert : Biome::RockyDesert;
    }
    if (humidity < params.steppeHumidity) {
        return Biome::Steppe;
    }
    if (humidity < params.forestHumidity) {
        return temperature >= params.mediterraneanTemperature ? Biome::Mediterranean
                                                              : Biome::Grassland;
    }
    return Biome::TemperateForest;
}

Biome hotBiome(float humidity, const BiomeParams& params) {
    if (humidity < params.hotDesertHumidity) {
        return Biome::RockyDesert;
    }
    if (humidity < params.shrublandHumidity) {
        return Biome::Shrubland;
    }
    if (humidity < params.dryForestHumidity) {
        return Biome::Savanna;
    }
    return humidity < params.rainforestHumidity ? Biome::DryForest : Biome::TropicalForest;
}

} // namespace

const char* biomeName(Biome biome) {
    return kBiomeNames[static_cast<std::size_t>(biome)];
}

float coastDistance(const SurfaceSite& site) {
    return static_cast<float>(static_cast<double>(site.continentalness) * kContinentalMeters);
}

bool isDesert(Biome biome) {
    return biome == Biome::RockyDesert || biome == Biome::SandDesert || biome == Biome::ColdDesert;
}

Biome whittakerBiome(float temperature, float humidity, const BiomeParams& params) {
    if (temperature < params.tundraTemperature) {
        return humidity < params.coldDesertHumidity ? Biome::ColdDesert : Biome::Tundra;
    }
    if (temperature < params.taigaTemperature) {
        if (humidity < params.coldDesertHumidity) {
            return Biome::ColdDesert;
        }
        return humidity < params.taigaMinHumidity ? Biome::Tundra : Biome::Taiga;
    }
    if (temperature < params.tropicalTemperature) {
        return temperateBiome(temperature, humidity, params);
    }
    return hotBiome(humidity, params);
}

Biome classifyBiome(const ClimateSample& climate, const SurfaceSite& site,
                    const BiomeParams& params) {
    if (site.water == WaterKind::Ocean) {
        return Biome::Ocean;
    }
    const float altitude = site.ground - kSeaLevel;
    Biome cold = Biome::Ocean;
    if (coldBiome(climate, params, cold)) {
        return cold;
    }
    // The high mountain is the coldest part of the mountain (its tree line is warmer).
    if (isMountain(climate, site, altitude, params)) {
        return climate.temperature <= params.snowTemperature + params.highMountainOffset
                   ? Biome::HighMountain
                   : Biome::Mountain;
    }
    // Too mild for ice, too cold for the tundra, where the cold comes from the latitude.
    if (polarCold(climate, params) && climate.temperature < params.tundraTemperature) {
        return Biome::Polar;
    }
    // A sea of dunes keeps far from the coasts (ErgParams::coastNear): its low floor is no beach.
    if (!(site.erg > 0.0f) && isBeach(climate, site, altitude, params)) {
        return Biome::Beach;
    }
    if (isMarsh(climate, site, altitude, params)) {
        return Biome::Marsh;
    }
    if (site.erg >= params.sandDesertErg) {
        return Biome::SandDesert;
    }
    return whittakerBiome(climate.temperature, climate.humidity, params);
}

} // namespace terrain
