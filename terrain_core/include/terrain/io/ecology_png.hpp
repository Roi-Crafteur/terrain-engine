#pragma once

#include "terrain/generation/biome.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/surface_material.hpp"
#include "terrain/io/png.hpp"

#include <array>
#include <cstdint>
#include <filesystem>

namespace terrain {

using Rgb8 = std::array<std::uint8_t, 3>;

// Colors of the biome map, in Biome order.
inline constexpr std::array<Rgb8, kBiomeCount> kBiomeRgb = {{
    {30, 60, 140},   // ocean
    {235, 220, 160}, // beach
    {230, 185, 100}, // desert
    {190, 180, 80},  // savanna
    {140, 200, 90},  // grassland
    {50, 130, 60},   // temperate forest
    {10, 85, 30},    // tropical forest
    {55, 100, 90},   // taiga
    {165, 170, 140}, // tundra
    {125, 110, 100}, // mountain
    {245, 245, 250}, // snow
    {95, 115, 70},   // marsh
}};

// Colors of the surface material map, in SurfaceMaterial order.
inline constexpr std::array<Rgb8, kSurfaceMaterialCount> kMaterialRgb = {{
    {40, 90, 200},   // water
    {225, 205, 140}, // sand
    {90, 160, 60},   // grass
    {130, 95, 60},   // dirt
    {120, 120, 120}, // rock
    {250, 250, 255}, // snow
    {80, 65, 45},    // mud
    {175, 170, 155}, // pebbles
}};

// Ramps of the vegetation density map: on land from bare ground (density 0) to dense
// vegetation (density 1), on water from open water to dense seaweed.
inline constexpr Rgb8 kBareGroundRgb = {60, 50, 40};
inline constexpr Rgb8 kDenseVegetationRgb = {20, 200, 40};
inline constexpr Rgb8 kOpenWaterRgb = {15, 30, 70};
inline constexpr Rgb8 kDenseSeaweedRgb = {20, 170, 150};

// Biome map, RGB: the color of the biome of each sample.
[[nodiscard]] Image renderBiomeMap(const EcologyMap& map);

// Surface material map, RGB: the color of the material of each sample.
[[nodiscard]] Image renderSurfaceMaterialMap(const EcologyMap& map);

// Vegetation density map, RGB: the total density of each sample (the probability of a plant
// in a cell of the placement lattice) on the land ramp, or on the water ramp where the
// material is water.
[[nodiscard]] Image renderVegetationDensityMap(const EcologyMap& map);

// Writes biome_map.png, surface_material_map.png and vegetation_density_map.png into
// directory, creating it if needed. Returns false if a file cannot be written.
[[nodiscard]] bool exportEcologyPngs(const EcologyMap& map, const std::filesystem::path& directory);

} // namespace terrain
