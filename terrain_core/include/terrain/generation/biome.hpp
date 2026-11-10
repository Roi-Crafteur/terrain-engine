#pragma once

#include "terrain/generation/climate.hpp"
#include "terrain/generation/terrain_surface.hpp"

#include <cstdint>

namespace terrain {

enum class Biome : std::uint8_t {
    Ocean,
    Beach,
    // The hot desert of regs (gravel) and hamadas (bare stone): the most common desert (agent 49;
    // it was Desert, sand everywhere).
    RockyDesert,
    Savanna,
    Grassland,
    TemperateForest,
    TropicalForest,
    Taiga,
    Tundra,
    Mountain,
    Snow,
    Marsh,
    // Agent 49.
    DryForest,     // the hot forests of a long dry season
    Steppe,        // the semi-arid grasslands of the temperate lands
    SandDesert,    // a sea of dunes (an erg of ergs.hpp): the biome follows the ergs, never places them
    ColdDesert,    // the deserts of cold winters: gravel, stone, sparse shrubs
    HighMountain,  // above the alpine meadows: rock, scree and patches of snow
    Polar,         // the polar region: frozen rock and gravel, too dry or too mild for ice
    IceDesert,     // the ice sheets: permanent snow where the cold comes from the latitude
    Mediterranean, // garrigue and maquis: warm, semi-dry shrublands
    Shrubland,     // the hot semi-arid brush between the deserts and the savannas
};

inline constexpr int kBiomeCount = 21;

// The ids kept for the biomes of the other agents (agent 49: none of theirs enters before), in
// the 64 of the blend texels (biome_blend.hpp: kBiomeIdBits); listed in docs/ROADMAP.md.
// - 21-22: agent 45, the volcanoes (lava field, geothermal zone);
// - 23: agent 47, the glaciers;
// - 24-33: agent 50, the wetlands and the coasts (swamp, bog, mangrove, salt marsh, flooded
//   forest, wet meadow, oasis, lagoon, floodplain, foreshore);
// - 34-39: agent 51, the seas (reef, pack ice, abyss, seamount, trench, atoll lagoon);
// - 40-63: free (the fantasy biomes, later).

// Lowercase English name of a biome, such as "temperate forest".
[[nodiscard]] const char* biomeName(Biome biome);

// Temperatures in degrees Celsius (corrected by altitude and exposure), humidities in [0, 1],
// slopes rise over run, altitudes in meters above sea level, distances in meters.
// The thresholds of the cold biomes follow snowTemperature by named offsets (agent 49): the snow
// and the frost will share one threshold (agent 31), and these biomes move with it.
struct BiomeParams {
    // Snow: permanent snow and ice where the temperature is at most snowTemperature, on ground
    // no steeper than snowMaxSlope (steeper faces shed the snow).
    float snowTemperature = -6.0f;
    float snowMaxSlope = 0.8f;
    // The snow of an ice sheet (ice desert) where the cold comes from the latitude: the base
    // temperature (at sea level) at most snowTemperature + iceDesertBaseOffset. Elsewhere, snow.
    float iceDesertBaseOffset = 0.0f;
    // The polar region: the base temperature at most snowTemperature + polarBaseOffset, and
    // either below the snow but drier than polarDryHumidity (too dry for ice: frozen rock), or
    // colder than tundraTemperature without snow (too mild for ice, too cold for the tundra).
    float polarBaseOffset = -4.0f;
    float polarDryHumidity = 0.25f;

    // Mountain: from mountainMinAltitude up, above the tree line (temperature at most
    // treelineTemperature) or on slopes of at least mountainSlope (except the walls of the
    // cliffs, steep at the scale of a sample only).
    float mountainMinAltitude = 1200.0f;
    float treelineTemperature = -1.0f;
    float mountainSlope = 0.7f;
    // High mountain: the mountain colder than snowTemperature + highMountainOffset.
    float highMountainOffset = 2.5f;

    // Beach: land at most beachMaxHeight above sea level (not below it: a depression; never in a
    // sea of dunes, SurfaceSite::erg, far from the coasts), on slopes up to beachMaxSlope, and
    // near the sea: the coast signal of the geography at most beachCoastReach meters inland
    // (coastDistance; at sea, an island, it always is).
    float beachMaxHeight = 4.0f;
    float beachMaxSlope = 0.1f;
    float beachCoastReach = 2500.0f;

    // Marsh: flat (slope up to marshMaxSlope), low (up to marshMaxAltitude), humid (at least
    // marshMinHumidity) and not frozen (at least marshMinTemperature) ground, within
    // marshRiverReach meters of a river, or at most marshCoastHeight above sea level within
    // marshCoastReach meters of the coast (coastDistance): never a basin far from the sea.
    float marshMaxSlope = 0.03f;
    float marshMaxAltitude = 300.0f;
    float marshMinHumidity = 0.7f;
    float marshMinTemperature = 2.0f;
    float marshRiverReach = 400.0f;
    float marshCoastHeight = 15.0f;
    float marshCoastReach = 5000.0f;

    // A sea of dunes: the sand desert where the erg weighs at least sandDesertErg
    // (SurfaceSite::erg), whatever the climate says (live dunes hold no plant).
    float sandDesertErg = 0.2f;

    // Whittaker diagram of the other land:
    // - colder than tundraTemperature: tundra, or cold desert drier than coldDesertHumidity;
    // - colder than taigaTemperature: taiga, tundra drier than taigaMinHumidity, cold desert drier
    //   than coldDesertHumidity;
    // - colder than tropicalTemperature (temperate): desert drier than temperateDesertHumidity
    //   (cold desert under coldDesertTemperature, rocky desert above), steppe drier than
    //   steppeHumidity, then grassland (garrigue and maquis from mediterraneanTemperature on)
    //   drier than forestHumidity, then temperate forest;
    // - hot: rocky desert drier than hotDesertHumidity, shrubland drier than shrublandHumidity,
    //   savanna drier than dryForestHumidity, dry forest drier than rainforestHumidity, then
    //   tropical forest.
    float tundraTemperature = -2.0f;
    float taigaTemperature = 4.0f;
    float taigaMinHumidity = 0.25f;
    float coldDesertHumidity = 0.12f;
    float tropicalTemperature = 20.0f;
    float temperateDesertHumidity = 0.2f;
    float coldDesertTemperature = 12.0f;
    float steppeHumidity = 0.3f;
    float mediterraneanTemperature = 15.0f;
    float forestHumidity = 0.45f;
    float hotDesertHumidity = 0.3f;
    float shrublandHumidity = 0.37f;
    float dryForestHumidity = 0.5f;
    float rainforestHumidity = 0.6f;
};

// The distance from a column to the coast, meters, signed: positive inland, negative at sea (the
// coast signal of the geography: SurfaceSite::continentalness times kContinentalMeters).
[[nodiscard]] float coastDistance(const SurfaceSite& site);

// Biome of a column, from its climate and its surface, by priority: ocean (the column is under
// the sea), the cold (polar region, ice desert, snow), high mountain, mountain, the polar region
// too mild for ice, beach, marsh, sand desert, then the Whittaker diagram. River columns take
// the biome of the land around them. Pure function: it never changes a height.
[[nodiscard]] Biome classifyBiome(const ClimateSample& climate, const SurfaceSite& site,
                                  const BiomeParams& params = {});

// Biome of the Whittaker diagram alone (land that is none of the biomes before it).
[[nodiscard]] Biome whittakerBiome(float temperature, float humidity,
                                   const BiomeParams& params = {});

// Whether a biome is a desert (rocky, sand or cold): its ground is mineral, grass only near the
// water.
[[nodiscard]] bool isDesert(Biome biome);

} // namespace terrain
