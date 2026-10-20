#pragma once

#include "terrain/generation/biome.hpp"
#include "terrain/generation/climate.hpp"
#include "terrain/generation/surface_material.hpp"
#include "terrain/generation/terrain_surface.hpp"

#include <array>
#include <cstdint>

namespace terrain {

// Boulders are decor rather than plants, but they are placed the same way.
enum class PlantType : std::uint8_t {
    Tree,
    Bush,
    Flower,
    TallGrass,
    Cactus,
    Boulder,
    Seaweed, // the only aquatic type
};

inline constexpr int kPlantTypeCount = 7;

// Lowercase English name of a plant type, such as "tall grass".
[[nodiscard]] const char* plantName(PlantType type);

// Probability of a plant of each type in one cell of the placement lattice. The sum is at
// most 1: a cell holds at most one plant.
using PlantDensities = std::array<float, kPlantTypeCount>;

[[nodiscard]] constexpr std::uint8_t materialBit(SurfaceMaterial material) {
    return static_cast<std::uint8_t>(1U << static_cast<unsigned>(material));
}

// Where a type may grow: on the materials of the mask, on ground no steeper than maxSlope
// (see steepestSlope; seaweed: the slope of the sea floor).
struct PlantRule {
    float maxSlope = 0.0f;
    std::uint8_t materials = 0; // materialBit of each material
};

// Densities of each biome (rows in Biome order, columns in PlantType order), before the
// clumps and the slope fade.
inline constexpr std::array<PlantDensities, kBiomeCount> kDefaultBiomeDensity = {{
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.3f},       // ocean
    {0.0f, 0.02f, 0.0f, 0.05f, 0.0f, 0.02f, 0.0f},    // beach
    {0.0f, 0.03f, 0.0f, 0.01f, 0.04f, 0.03f, 0.0f},   // desert
    {0.03f, 0.08f, 0.02f, 0.4f, 0.005f, 0.01f, 0.0f}, // savanna
    {0.01f, 0.04f, 0.12f, 0.45f, 0.0f, 0.01f, 0.0f},  // grassland
    {0.35f, 0.15f, 0.03f, 0.15f, 0.0f, 0.02f, 0.0f},  // temperate forest
    {0.5f, 0.25f, 0.05f, 0.1f, 0.0f, 0.01f, 0.0f},    // tropical forest
    {0.3f, 0.1f, 0.01f, 0.1f, 0.0f, 0.03f, 0.0f},     // taiga
    {0.0f, 0.06f, 0.03f, 0.2f, 0.0f, 0.05f, 0.0f},    // tundra
    {0.0f, 0.02f, 0.01f, 0.03f, 0.0f, 0.1f, 0.0f},    // mountain
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},       // snow
    {0.05f, 0.1f, 0.02f, 0.5f, 0.0f, 0.0f, 0.0f},     // marsh
}};

inline constexpr std::array<PlantRule, kPlantTypeCount> kDefaultPlantRules = {{
    {0.7f, materialBit(SurfaceMaterial::Grass) | materialBit(SurfaceMaterial::Dirt) |
               materialBit(SurfaceMaterial::Mud)}, // tree
    {0.8f, materialBit(SurfaceMaterial::Grass) | materialBit(SurfaceMaterial::Dirt) |
               materialBit(SurfaceMaterial::Sand) | materialBit(SurfaceMaterial::Mud) |
               materialBit(SurfaceMaterial::Pebbles)}, // bush
    {0.5f, materialBit(SurfaceMaterial::Grass)},       // flower
    {0.6f, materialBit(SurfaceMaterial::Grass) | materialBit(SurfaceMaterial::Dirt) |
               materialBit(SurfaceMaterial::Mud)},                                   // tall grass
    {0.5f, materialBit(SurfaceMaterial::Sand) | materialBit(SurfaceMaterial::Dirt)}, // cactus
    {0.9f, materialBit(SurfaceMaterial::Rock) | materialBit(SurfaceMaterial::Pebbles) |
               materialBit(SurfaceMaterial::Dirt) | materialBit(SurfaceMaterial::Grass) |
               materialBit(SurfaceMaterial::Sand)}, // boulder
    {0.5f, materialBit(SurfaceMaterial::Water)},    // seaweed
}};

struct VegetationParams {
    // Side of the cells of the placement lattice, in meters: cell (i, k) covers
    // [i, i + 1) x [k, k + 1) * size in world coordinates and holds at most one plant, size
    // being cellSize adjusted to a whole number of cells per side of the world
    // (VegetationField::cellsX, exactly cellSize when it divides the world).
    double cellSize = 4.0;
    std::array<PlantDensities, kBiomeCount> biomeDensity = kDefaultBiomeDensity;
    std::array<PlantRule, kPlantTypeCount> rules = kDefaultPlantRules;

    // Clumps: c = clamp(0.5 + fbm, 0, 1) with 3 octaves. Trees and bushes are scaled by
    // 1 + clumpContrast * (2c - 1) (woods), flowers and tall grass by the mirror factor
    // 1 - clumpContrast * (2c - 1) (glades).
    double clumpWavelength = 400.0;
    float clumpContrast = 0.7f;

    // Seaweed only grows in the ocean, on floors from minSeaweedDepth to maxSeaweedDepth
    // meters below the surface.
    float minSeaweedDepth = 1.0f;
    float maxSeaweedDepth = 25.0f;

    // A plant is only kept if the exact surface around it, probed probeStep meters away in the
    // 4 directions, is no steeper than the maxSlope of its type: never on a wall.
    double probeStep = 1.0;

    // Size factor of a plant, uniform in [minScale, maxScale].
    float minScale = 0.7f;
    float maxScale = 1.3f;
};

// Random draw of a cell of the placement lattice: position of its candidate plant, value
// choosing its type, size factor and rotation about the vertical axis.
struct LatticeDraw {
    double x = 0.0;
    double z = 0.0;
    double pick = 0.0;     // [0, 1)
    float scale = 1.0f;    // [minScale, maxScale]
    float rotation = 0.0f; // [0, 2 pi] radians
};

// A plant, or a boulder, standing on the surface.
struct Plant {
    PlantType type = PlantType::Tree;
    double x = 0.0;
    double z = 0.0;
    float y = 0.0f; // base: the exact surface at (x, z), the sea floor for seaweed
    float scale = 1.0f;
    float rotation = 0.0f;

    friend bool operator==(const Plant&, const Plant&) = default;
};

// Deterministic vegetation: the same seed, parameters and inputs always give the same
// densities and draws. Draws only depend on the lattice cell, never on a region, so plants
// join without a seam across regions and chunks. Vegetation never changes a height.
// Periodic on the world (WorldSize): the clumps tile it, and so does the lattice, of
// round(world / cellSize) cells per side (PeriodicCells).
class VegetationField {
public:
    // Throws std::invalid_argument for an invalid world (validate).
    explicit VegetationField(std::uint64_t seed, const VegetationParams& params = {},
                             const WorldSize& world = {});

    // Clump factor c at (x, z), in [0, 1].
    [[nodiscard]] float clumpAt(double x, double z) const;

    // True if a plant of the type may grow on the column: a land type on dry, solid ground
    // (no rock above air, no cliff wall) of a material of its rule, no steeper than its
    // maxSlope; seaweed in the ocean between the seaweed depths, on a gentle floor.
    [[nodiscard]] bool canGrow(PlantType type, SurfaceMaterial material,
                               const ClimateSample& climate, const SurfaceSite& site) const;

    // Densities of a column at (x, z): the biome densities scaled by the clumps and faded out
    // from half the maxSlope of each type to it, 0 wherever canGrow is false, scaled down to a
    // sum of 1 if needed.
    [[nodiscard]] PlantDensities densities(double x, double z, Biome biome,
                                           SurfaceMaterial material, const ClimateSample& climate,
                                           const SurfaceSite& site) const;

    // Draw of the cell (i, k) of the lattice, an unwrapped index: hashed from its canonical
    // index, its position is the image of the canonical one next to the cell.
    [[nodiscard]] LatticeDraw drawCell(std::int64_t i, std::int64_t k) const;

    [[nodiscard]] const VegetationParams& params() const { return params_; }
    [[nodiscard]] const WorldSize& world() const { return world_; }
    // The lattice of the plants along x and z.
    [[nodiscard]] const PeriodicCells& cellsX() const { return cellsX_; }
    [[nodiscard]] const PeriodicCells& cellsZ() const { return cellsZ_; }

private:
    VegetationParams params_;
    WorldSize world_;
    PeriodicCells cellsX_;
    PeriodicCells cellsZ_;
    std::uint64_t clumpSeed_;
    std::uint64_t placementSeed_;
};

// Type drawn by pick in [0, 1) from densities summing to at most 1: the first type whose
// cumulative density exceeds pick, or false if pick is above their sum (no plant).
[[nodiscard]] bool pickPlant(const PlantDensities& densities, double pick, PlantType& type);

// Sum of the densities: the probability of a plant in a cell of the lattice.
[[nodiscard]] float totalDensity(const PlantDensities& densities);

} // namespace terrain
