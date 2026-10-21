#pragma once

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_grid.hpp"

#include <cstdint>
#include <vector>

namespace terrain::planet {

// Altitude of every cell of the grid, in meters: Heightfield::heightAt at the cell center, or
// with samplesPerAxis > 1 the mean of samplesPerAxis^2 points at the centers of a regular
// subdivision of the cell (a box filter against the aliasing of the ridged noise). Computed on
// up to threads threads (resolveThreads); every cell gets the same bits whatever their number.
// Throws std::invalid_argument for samplesPerAxis outside [1, 8], or if the field and the grid
// do not tile the same world, std::domain_error for a non-finite altitude.
[[nodiscard]] std::vector<float> sampleRelief(const Heightfield& field, const PlanetGrid& grid,
                                              int samplesPerAxis, int threads);

// Water kinds of the cells, in an ocean mask.
enum OceanKind : std::uint8_t {
    kLand = 0,
    kOcean = 1,
    kForcedOutlet = 2, // the lowest cell of a world without ocean, a sink like the ocean
};

// Ocean mask: the cells below sea level (kSeaLevel) whose 8-connected below-sea component, on
// the torus, covers at least minOceanArea square meters are kOcean; smaller components are
// land (depressions, lakes). Without any ocean, the lowest cell (by altitude, then index) is a
// kForcedOutlet, so that all the water of the world has somewhere to go.
[[nodiscard]] std::vector<std::uint8_t>
markOcean(const PlanetGrid& grid, const std::vector<float>& height, double minOceanArea);

// The strict total order of the cells used by the whole hydrology: by altitude, then by index.
// No two cells are equal, so there are no flats.
[[nodiscard]] inline bool lowerCell(const std::vector<float>& height, std::size_t a,
                                    std::size_t b) {
    return height[a] < height[b] || (!(height[b] < height[a]) && a < b);
}

} // namespace terrain::planet
