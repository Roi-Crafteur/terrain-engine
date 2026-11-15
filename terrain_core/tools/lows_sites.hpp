#pragma once

// The water of the closed lows of a world by the climate of its planet level (terrain_export_lows,
// agent 52): for each low, the water at its lowest point (dry, salt lake, lake) and the climate
// belt at its center, to show that the dry ones lie mostly in the deserts. The belts read the
// aridity index P / PET and the temperature of the planet level.

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_level.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace lows_tool {

enum class Belt : int { Cold, HyperArid, Arid, SemiArid, SubHumid, Humid, Unknown };
inline constexpr std::size_t kBelts = 7;
[[nodiscard]] const char* beltName(Belt belt);

// The belt at (x, z): cold under 0 C, then by P / PET (0.05, 0.2, 0.5, 0.65); Unknown without
// the planet level.
[[nodiscard]] Belt beltAt(const terrain::PlanetLevel* planet, double x, double z);

// The water of a low: what lies at its lowest point.
enum class LowWater : int { Dry, Salt, Lake, Other };
inline constexpr std::size_t kLowWaters = 4;
[[nodiscard]] const char* lowWaterName(LowWater water);

// A low to measure: its family, center, how far it reaches (meters), and its radius, floor and
// wear (terrain::LowSite).
struct Low {
    std::string family;
    double x = 0.0;
    double z = 0.0;
    double reach = 0.0;
    double radius = 0.0;
    float floor = 0.0f;
    float wear = 0.0f;
};

struct LowFigures {
    Low low;
    double lowX = 0.0;
    double lowZ = 0.0;
    float lowest = 0.0f; // the ground at the lowest point
    LowWater water = LowWater::Other;
    Belt belt = Belt::Unknown;
};

// The lowest point of a low on a grid of 65 x 65 points over its reach, the water there and the
// belt at its center.
[[nodiscard]] LowFigures measureLow(const terrain::Heightfield& field,
                                    const terrain::PlanetLevel* planet, const Low& low);

// Counts of the lows of one family by belt and water.
struct BeltTable {
    std::string family;
    std::array<std::array<std::size_t, kLowWaters>, kBelts> counts{};
};
[[nodiscard]] std::vector<BeltTable> beltTables(const std::vector<LowFigures>& lows);

// The craters by class of diameter (0.4 to 2, 2 to 10, 10 to 60 km: impact_craters.hpp) and by
// wear (fresh under 1/3, worn, worn out from 2/3), and by belt and wear.
inline constexpr std::size_t kCraterClasses = 3;
inline constexpr std::size_t kWears = 3;
[[nodiscard]] const char* wearName(std::size_t wear);
struct CraterTable {
    std::array<std::array<std::size_t, kWears>, kCraterClasses> byClass{};
    std::array<std::array<std::size_t, kWears>, kBelts> byBelt{};
    std::size_t belowSea = 0; // whose lowest point lies below 0 m
};
[[nodiscard]] CraterTable craterTable(const std::vector<LowFigures>& lows);

// The lows of the field (Heightfield::lowsIn over the whole world).
[[nodiscard]] std::vector<Low> lowsOf(const terrain::Heightfield& field);

} // namespace lows_tool
