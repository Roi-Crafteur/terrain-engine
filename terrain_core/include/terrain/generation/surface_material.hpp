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
    // Agent 49: the soils of the deserts.
    Gravel, // a reg: a plain of gravel, the floor of the ergs and the polar region
    Stone,  // a hamada: a plateau of bare stone
    Crust,  // a crust of clay or salt
};

inline constexpr int kSurfaceMaterialCount = 11;

// The ids kept for the materials of the other agents (agent 49: none of theirs enters before), in
// the 32 of the blend texels (biome_blend.hpp: kMaterialIdBits); listed in docs/ROADMAP.md.
// - 11-14: agent 45, the volcanoes (ash, basalt and cold lava, scoria, sulfur);
// - 15-16: agent 46, the plateaus and the karst (limestone, red sandstone);
// - 17-18: agent 47, the glaciers and the scree (glacier ice, moraine);
// - 19-22: agent 50, the wetlands and the coasts (peat, salt crust, mudflat, shingle);
// - 23-25: agent 51, the seas (coral, sea ice, abyssal ooze);
// - 26-31: free.

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
    // The deserts (isDesert, agent 49): grass only within desertGrassReach meters of a river
    // (pebbles on its banks, riverBankWidth); away from it, sand in the sand deserts, and in the
    // rocky and cold deserts a hamada (stone) from hamadaAltitude meters up, a reg (gravel) below.
    float desertGrassReach = 150.0f;
    float hamadaAltitude = 450.0f;
};

// The material of the floor of an erg of that kind (SurfaceSite::ergFloor): sand under 0.5,
// gravel up to 1.5, crust above.
[[nodiscard]] SurfaceMaterial ergFloorMaterial(float kind);

// Steepest local slope of a column: the larger of |grad H| and the steepness of the site.
[[nodiscard]] float steepestSlope(const ClimateSample& climate, const SurfaceSite& site);

// Material at the top of a column. Rock where the site is bare rock (SurfaceSite::bareRock, even
// under the water); else water exactly over the ocean and the rivers; otherwise sand on the dunes
// of the ergs at any slope (but on walls and rock above air), rock on steep ground, walls and rock
// above air, the floor of the ergs between their dunes away from the water (ergFloorMaterial),
// then by biome: snow (snow, ice desert), mountain snow, rock and scree (mountain, high
// mountain), beach sand or pebbles, the gravel of the polar region, the deserts (grass and
// pebbles near a river, else sand, stone or gravel), marsh mud, river bank pebbles, tundra dirt,
// dirt on steeper ground and grass elsewhere. Pure function: it never changes a height.
[[nodiscard]] SurfaceMaterial surfaceMaterial(Biome biome, const ClimateSample& climate,
                                              const SurfaceSite& site,
                                              const MaterialParams& params = {});

} // namespace terrain
