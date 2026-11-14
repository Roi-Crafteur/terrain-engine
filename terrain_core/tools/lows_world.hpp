#pragma once

// The land below the sea level on a grid of the whole world (terrain_export_lows, agent 52): its
// shares, medians and water, its largest dry stretches, and a map of it. The land is every sample
// the field does not put under the sea (HeightSample::water), whatever its height: a dry
// depression below the sea level is land.

#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/io/png.hpp"
#include "terrain/io/relief_png.hpp"

#include <cstddef>
#include <vector>

namespace lows_tool {

// A stretch of dry land below the sea level: 8-connected samples of the grid (on the torus).
struct Stretch {
    double area = 0.0; // square meters
    std::size_t samples = 0;
    float lowest = 0.0f; // meters
    double lowX = 0.0;   // where it lies
    double lowZ = 0.0;
};

struct WorldFigures {
    std::size_t samples = 0;
    std::size_t land = 0;      // not the sea
    std::size_t below0 = 0;    // land below 0 m (its ground)
    std::size_t below50 = 0;   // land below +50 m
    std::size_t dryBelow = 0;  // land below 0 m: dry, salt lake, lake, river
    std::size_t saltBelow = 0;
    std::size_t lakeBelow = 0;
    std::size_t riverBelow = 0;
    float medianLand = 0.0f;   // the median height of the land
    float medianDepth = 0.0f;  // the median depth of the land below 0 m (positive)
    std::vector<Stretch> largest; // the largest dry stretches below 0 m, largest first
};

// The water of the planet level over the land below the sea level of a grid of the relief alone
// (the fine water of the whole world would compute every river): a lake of the planet where its
// level stands over the ground (a salt lake unless it overflows or is frozen, as the hydrology
// says), dry elsewhere, the cells of its rivers counted in riverCells.
void addPlanetWater(terrain::ReliefGrid& grid, const terrain::PlanetLevel& planet,
                    std::size_t& riverCells);

// The figures of the grid (it must wrap), keeping the keep largest stretches.
[[nodiscard]] WorldFigures measureWorld(const terrain::ReliefGrid& grid, std::size_t keep);

// The land below the sea level on a map width pixels wide (a whole number of samples per
// pixel): dry in red, salt lakes in white, lakes and rivers below 0 m in cyan, over the land
// shaded grey by its height and the sea in dark blue. A pixel shows the rarest class of its
// samples (dry before salt before water before land before sea): the small stretches stay seen.
[[nodiscard]] terrain::Image lowsMap(const terrain::ReliefGrid& grid, int width);

} // namespace lows_tool
