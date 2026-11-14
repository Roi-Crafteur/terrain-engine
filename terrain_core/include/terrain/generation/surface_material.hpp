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
    // The dunes of the seas of dunes (SurfaceSite::dunes) at least this many meters deep are sand,
    // whatever their slope and their biome (in the ergs, their band duneSandWidth meters wide on
    // either side: the sand thins out over the floor between them, as irregular as the noise of
    // the borders).
    float duneSand = 1.5f;
    float duneSandWidth = 1.5f;
    // The floor between them (agent 34), where the erg weighs at least ergFloorErg (its mask, and
    // on its rock out of the sand kErgFloorShare of it: half of that) and the nearest river is
    // farther than ergGrassReach: sand, gravel (pebbles) or crust (dirt) by its kind
    // (SurfaceSite::ergFloor: under 0.5, up to 1.5, above; its bands ergFloorWidth wide), never
    // grass; nearer the water and beyond its edges, the material of its biome.
    float ergFloorErg = 0.06f;
    float ergGrassReach = 150.0f;
    float ergFloorWidth = 0.2f;
    // The ground of the volcanoes (agent 45, SurfaceSite::volcanicGround) where it lies at least
    // volcanicShare bare (its band volcanicWidth wide), but under the snow (the Snow biome, the
    // snow of the mountains): its material (volcanicMaterial), never grass.
    float volcanicShare = 0.5f;
    float volcanicWidth = 0.1f;
};

// The material of the floor of an erg of that kind (SurfaceSite::ergFloor): sand under 0.5,
// gravel (pebbles) up to 1.5, crust (dirt) above.
[[nodiscard]] SurfaceMaterial ergFloorMaterial(float kind);

// The material of a volcanic ground (until its own colors, agent 49): basalt and cooled lava rock,
// scoria pebbles, ash dirt, geothermal ground mud.
[[nodiscard]] SurfaceMaterial volcanicMaterial(VolcanicGround ground);

// Steepest local slope of a column: the larger of |grad H| and the steepness of the site.
[[nodiscard]] float steepestSlope(const ClimateSample& climate, const SurfaceSite& site);

// Material at the top of a column. Rock where the site is bare rock (SurfaceSite::bareRock, even
// under the water); else water exactly over the ocean and the rivers; otherwise sand on the dunes
// of the ergs at any slope (but on walls and rock above air), rock on steep ground, walls and rock
// above air, the floor of the ergs between their dunes away from the water (ergFloorMaterial),
// the ground of the volcanoes out of the snow (volcanicMaterial), then by biome: snow, mountain
// snow, rock and scree, beach sand or pebbles, desert sand, marsh
// mud, river bank pebbles, tundra dirt, dirt on steeper ground and grass elsewhere. Pure
// function: it never changes a height.
[[nodiscard]] SurfaceMaterial surfaceMaterial(Biome biome, const ClimateSample& climate,
                                              const SurfaceSite& site,
                                              const MaterialParams& params = {});

} // namespace terrain
