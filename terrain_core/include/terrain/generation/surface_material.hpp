#pragma once

#include "terrain/generation/biome.hpp"
#include "terrain/generation/climate.hpp"
#include "terrain/generation/terrain_surface.hpp"

#include <cstdint>

namespace terrain {

// What covers the top of a column.
enum class SurfaceMaterial : std::uint8_t {
    Water, // ocean and rivers
    Sand,
    Grass,
    Dirt,
    Rock,
    Snow,
    Mud,
    Pebbles,
};

inline constexpr int kSurfaceMaterialCount = 8;

// Lowercase English name of a material, such as "pebbles".
[[nodiscard]] const char* materialName(SurfaceMaterial material);

// Slopes are rise over run, measured as the steepest of |grad H| and the steepness of the
// site; temperatures in degrees Celsius; distances in meters.
struct MaterialParams {
    // Bare rock on dry ground at least this steep, on the vertical walls of the cliffs and on
    // rock above air (an overhang or an arch holds no soil).
    float rockSlope = 1.0f;
    // Vegetated ground from dirtSlope up shows its soil: dirt.
    float dirtSlope = 0.55f;
    // Mountain: snow where the temperature is at most mountainSnowTemperature, scree (pebbles)
    // from screeSlope up, rock below.
    float mountainSnowTemperature = -4.0f;
    float screeSlope = 0.45f;
    // Beaches colder than this are pebbles, warmer ones sand.
    float pebbleBeachTemperature = 4.0f;
    // River banks: pebbles within riverBankWidth of a river sample, outside the deserts,
    // the beaches and the marshes.
    float riverBankWidth = 25.0f;
};

// Steepest local slope of a column: the larger of |grad H| and the steepness of the site.
[[nodiscard]] float steepestSlope(const ClimateSample& climate, const SurfaceSite& site);

// Material at the top of a column. Water exactly over the ocean and the rivers; otherwise
// rock on steep ground, walls and rock above air, then by biome: snow, mountain snow, rock
// and scree, beach sand or pebbles, desert sand, marsh mud, river bank pebbles, tundra dirt,
// dirt on steeper ground and grass elsewhere. Pure function: it never changes a height.
[[nodiscard]] SurfaceMaterial surfaceMaterial(Biome biome, const ClimateSample& climate,
                                              const SurfaceSite& site,
                                              const MaterialParams& params = {});

} // namespace terrain
