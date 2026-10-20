#pragma once

#include "terrain/generation/biome.hpp"
#include "terrain/generation/climate.hpp"
#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/overhangs.hpp"
#include "terrain/generation/surface_material.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/generation/vegetation.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain {

struct EcologyParams {
    ClimateParams climate;
    BiomeParams biomes;
    MaterialParams materials;
    VegetationParams vegetation;
};

// Everything the ecology gives a column.
struct EcologySample {
    ClimateSample climate;
    Biome biome = Biome::Ocean;
    SurfaceMaterial material = SurfaceMaterial::Water;
    PlantDensities densities{};
};

// Ecology of every sample of a surface, indexed like TerrainSurface::sites.
struct EcologyMap {
    HeightmapRegion region;
    std::vector<EcologySample> samples; // row by row, columns vary fastest

    [[nodiscard]] int size() const { return region.size; }
    [[nodiscard]] double sampleX(int column) const; // as TerrainSurface::sampleX
    [[nodiscard]] double sampleZ(int row) const;
    [[nodiscard]] std::size_t index(int column, int row) const {
        return static_cast<std::size_t>(row * region.size + column);
    }
    [[nodiscard]] const EcologySample& at(int column, int row) const {
        return samples[index(column, row)];
    }
};

// World rectangle [minX, maxX) x [minZ, maxZ). It covers the lattice cells whose corner
// (i, k) * cellSize lies inside, so adjacent areas (chunks) share no cell and miss none.
struct PlantArea {
    double minX = 0.0;
    double minZ = 0.0;
    double maxX = 0.0;
    double maxZ = 0.0;
};

// Climate, biomes, surface materials and vegetation laid on a relief, in a fixed order:
// climate, biome, material, then densities and plants. Deterministic: the same seed,
// parameters and surface always give the same result. It only reads the relief: no height
// is ever changed, and every plant stands on the exact surface it is given.
class Ecology {
public:
    // Periodic on the world, as its climate and vegetation. Throws std::invalid_argument for an
    // invalid world (validate).
    explicit Ecology(std::uint64_t seed, const EcologyParams& params = {},
                     const WorldSize& world = {});

    [[nodiscard]] EcologySample sampleAt(double x, double z, const SurfaceSite& site) const;
    [[nodiscard]] EcologyMap map(const TerrainSurface& surface) const;

    // Densities at (x, z), bilinear between the 4 samples of the map around it. A type is 0
    // unless all 4 samples allow it, so no plant grows next to a sample where it cannot grow
    // (a wall, a river, the ocean for a land plant). False outside the samples of the map.
    [[nodiscard]] bool densitiesAt(const EcologyMap& map, double x, double z,
                                   PlantDensities& densities) const;

    // Plants of the lattice cells of area, row by row: the candidate of each cell, its type
    // drawn from densitiesAt, standing at y = surface(x, z). surface must be the exact surface
    // the map was built from (Heightfield::heightAt, ErodedRegion::heightAt or
    // SculptedRegion::surfaceAt). A land plant is dropped below sea level, seaweed outside the
    // seaweed depths, and any plant where the surface probed around it is steeper than its
    // type allows. The plants of a cell never depend on the area, so placing two adjacent
    // areas gives exactly the plants of their union.
    [[nodiscard]] std::vector<Plant> placePlants(const EcologyMap& map, const ReliefHeight& surface,
                                                 const PlantArea& area) const;

    [[nodiscard]] const ClimateField& climate() const { return climate_; }
    [[nodiscard]] const VegetationField& vegetation() const { return vegetation_; }
    [[nodiscard]] const EcologyParams& params() const { return params_; }
    [[nodiscard]] const WorldSize& world() const { return climate_.world(); }

private:
    [[nodiscard]] bool keepPlant(const Plant& plant, const ReliefHeight& surface) const;

    EcologyParams params_;
    ClimateField climate_;
    VegetationField vegetation_;
};

} // namespace terrain
