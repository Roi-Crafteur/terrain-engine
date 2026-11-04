#pragma once

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_grid.hpp"

#include <array>
#include <cstddef>
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

// The altitudes of sampleRelief, and the sea of every cell: 1 where the field puts the sea at its
// center (HeightSample::water: where the geography puts it, not below kSeaLevel), else 0. With
// floorSamples above 1, the cells of the land and of the sea within kFloorCoast meters of it
// take instead their floor, the lowest of floorSamples^2 points at the centers of a regular
// subdivision of the cell of the same water as its center (the land of a cell on the coast, not
// the bottom of the sea; its center when there is none) (floor: its index, row by row;
// kCenterFloor for the center alone or a mean), and mountain says whether the mountain mask is
// above 0 there.
// With floorSamples above 1, the cells under the mountains (mountain 1) whose points rise more than
// kExitLeast above their floor also keep their exits (planet_links.hpp), by row, sorted by column:
// for each of their 8 neighbors (kPlanetNeighbors), the lowest height a way from their floor point
// to their side facing that neighbor must climb to, over their floorSamples^2 points
// (8-connected), in steps of kExitStep meters above their floor (rounded up, at most 255 steps);
// and their floor, in meters rounded down.
inline constexpr double kFloorCoast = 16'384.0;
inline constexpr std::uint8_t kCenterFloor = 255;
inline constexpr double kExitStep = 8.0;
inline constexpr double kExitLeast = 20.0;
struct CellExits {
    std::uint16_t column = 0;
    std::int16_t floor = 0;
    std::array<std::uint8_t, 8> climb{};
    [[nodiscard]] double exit(std::size_t k) const {
        return static_cast<double>(floor) + kExitStep * static_cast<double>(climb[k]);
    }
};
struct CellRelief {
    std::vector<float> height;
    std::vector<std::uint8_t> sea;
    std::vector<std::uint8_t> floor;
    std::vector<std::uint8_t> mountain;
    std::vector<std::vector<CellExits>> exits; // by row
};
// The exits of a cell of a column over its n^2 points of heights (row by row), from its floor
// point floorIndex.
[[nodiscard]] CellExits cellExits(int column, const float* heights, int n, int floorIndex);
[[nodiscard]] CellRelief sampleCells(const Heightfield& field, const PlanetGrid& grid,
                                     int samplesPerAxis, int threads, int floorSamples = 1);

// The floor index of a cell as sampleCells gives it (kCenterFloor with one sample).
[[nodiscard]] std::uint8_t floorOf(const Heightfield& field, const PlanetGrid& grid,
                                   std::size_t cell, int floorSamples);

// The point of floor index floor of cell (i, j) with floorSamples^2 points (the center for
// kCenterFloor), meters, in the canonical range of the cell.
[[nodiscard]] std::array<double, 2> floorPoint(const PlanetGrid& grid, std::size_t cell,
                                               std::uint8_t floor, int floorSamples);

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
// markOcean on the cells of the sea (sea[c] != 0) instead of the cells below sea level: a dry
// depression below the sea level is land.
[[nodiscard]] std::vector<std::uint8_t> markOcean(const PlanetGrid& grid,
                                                  const std::vector<float>& height,
                                                  const std::vector<std::uint8_t>& sea,
                                                  double minOceanArea);

// The strict total order of the cells used by the whole hydrology: by altitude, then by index.
// No two cells are equal, so there are no flats.
[[nodiscard]] inline bool lowerCell(const std::vector<float>& height, std::size_t a,
                                    std::size_t b) {
    return height[a] < height[b] || (!(height[b] < height[a]) && a < b);
}

} // namespace terrain::planet
