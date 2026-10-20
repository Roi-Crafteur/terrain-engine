#pragma once

#include "terrain/generation/climate.hpp"
#include "terrain/generation/terrain_surface.hpp"

#include <cstdint>

namespace terrain {

enum class Biome : std::uint8_t {
    Ocean,
    Beach,
    Desert,
    Savanna,
    Grassland,
    TemperateForest,
    TropicalForest,
    Taiga,
    Tundra,
    Mountain,
    Snow,
    Marsh,
};

inline constexpr int kBiomeCount = 12;

// Lowercase English name of a biome, such as "temperate forest".
[[nodiscard]] const char* biomeName(Biome biome);

// Temperatures in degrees Celsius (corrected by altitude and exposure), humidities in [0, 1],
// slopes rise over run, altitudes in meters above sea level.
struct BiomeParams {
    // Snow: permanent snow and ice where the temperature is at most snowTemperature, on ground
    // no steeper than snowMaxSlope (steeper faces shed the snow).
    float snowTemperature = -6.0f;
    float snowMaxSlope = 0.8f;

    // Mountain: from mountainMinAltitude up, above the tree line (temperature at most
    // treelineTemperature) or on slopes of at least mountainSlope (except the walls of the
    // cliffs, steep at the scale of a sample only).
    float mountainMinAltitude = 1200.0f;
    float treelineTemperature = -1.0f;
    float mountainSlope = 0.7f;

    // Beach: land at most beachMaxHeight above sea level, on slopes up to beachMaxSlope.
    float beachMaxHeight = 4.0f;
    float beachMaxSlope = 0.1f;

    // Marsh: flat (slope up to marshMaxSlope), low (up to marshMaxAltitude), humid (at least
    // marshMinHumidity) and not frozen (at least marshMinTemperature) ground, within
    // marshRiverReach meters of a river or at most marshCoastHeight above sea level.
    float marshMaxSlope = 0.03f;
    float marshMaxAltitude = 300.0f;
    float marshMinHumidity = 0.7f;
    float marshMinTemperature = 2.0f;
    float marshRiverReach = 400.0f;
    float marshCoastHeight = 15.0f;

    // Whittaker diagram of the other land: tundra below tundraTemperature, taiga below
    // taigaTemperature (tundra if drier than taigaMinHumidity), temperate biomes below
    // tropicalTemperature, hot biomes above.
    float tundraTemperature = -2.0f;
    float taigaTemperature = 4.0f;
    float taigaMinHumidity = 0.25f;
    float tropicalTemperature = 20.0f;
    // Temperate: desert below temperateDesertHumidity, grassland below forestHumidity, then
    // temperate forest.
    float temperateDesertHumidity = 0.2f;
    float forestHumidity = 0.45f;
    // Hot: desert below hotDesertHumidity, savanna below rainforestHumidity, then tropical
    // forest.
    float hotDesertHumidity = 0.3f;
    float rainforestHumidity = 0.6f;
};

// Biome of a column, from its climate and its surface, by priority: ocean (the column is under
// the sea), snow, mountain, beach, marsh, then the Whittaker diagram. River columns take the
// biome of the land around them. Pure function: it never changes a height.
[[nodiscard]] Biome classifyBiome(const ClimateSample& climate, const SurfaceSite& site,
                                  const BiomeParams& params = {});

// Biome of the Whittaker diagram alone (land that is neither snow, mountain, beach nor marsh).
[[nodiscard]] Biome whittakerBiome(float temperature, float humidity,
                                   const BiomeParams& params = {});

} // namespace terrain
